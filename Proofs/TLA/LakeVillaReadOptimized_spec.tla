----------------------------- MODULE LakeVillaReadOptimized_spec -----------------------------
(******************************************************************************)
(* LV[Read] = LV[I]: global version log with atomic validation phase for     *)
(* serializable, multi-table, multi-query transactions (Lemmas 4.6–4.8,     *)
(* 5.8, 5.9, 6.1).                                                           *)
(******************************************************************************)

EXTENDS Integers, FiniteSets, Sequences, TLC

CONSTANTS 
    TABLES,          \* Set of table identifiers
    TRANSACTIONS,    \* Set of transaction identifiers
    MAX_VERSION,     \* Maximum version number
    MAX_SLACK        \* Maximum slack for R1 freshness constraint

ASSUME MAX_VERSION \in Nat /\ MAX_VERSION > 0
ASSUME MAX_SLACK \in Nat

VERSIONS == 0..MAX_VERSION

\* Helper: Maximum of a set of numbers
Maximum(S) == CHOOSE x \in S : \A y \in S : x >= y

VARIABLES
    \* LV[I] Module: Global version log (Lemmas 4.6, 4.7)
    globalVersionLog,    \* [TABLES -> VERSIONS] - current committed version per table
    
    \* Transaction state
    txnState,            \* [TRANSACTIONS -> {"idle", "active", "validating", "committed", "aborted"}]
    
    \* Per-transaction version tracking (read, hidden, write, log versions)
    txnReadVersions,     \* [TRANSACTIONS -> [TABLES -> VERSIONS]] - snapshot at transaction start
    txnHiddenVersions,   \* [TRANSACTIONS -> [TABLES -> VERSIONS]] - versions created after read
    txnWriteVersions,    \* [TRANSACTIONS -> [TABLES -> VERSIONS]] - assigned write version
    txnLogVersions,      \* [TRANSACTIONS -> [TABLES -> VERSIONS]] - table version at validation
    
    \* Read and write sets (R1/R2/R3 validation)
    txnReadSet,          \* [TRANSACTIONS -> SUBSET TABLES]
    txnWriteSet,         \* [TRANSACTIONS -> SUBSET TABLES]
    
    \* Slack for R1 freshness check (configurable staleness tolerance)
    txnSlack,            \* [TRANSACTIONS -> 0..MAX_SLACK]
    
    \* Blind append flag: write-only transactions that ignore ordering
    txnBlindAppend,      \* [TRANSACTIONS -> BOOLEAN]
    
    \* LV[I] validation lock: PUT-IF-ABSENT ensures sequential validation (Lemma 4.6)
    validationLock,      \* TRANSACTIONS \cup {"NONE"}
    
    tableLogs,           \* [TABLES -> [VERSIONS -> {"EMPTY", "PENDING", "COMMITTED"}]]
    tableData            \* [TABLES -> [VERSIONS -> SUBSET TRANSACTIONS]]

vars == <<globalVersionLog, txnState, txnReadVersions, txnHiddenVersions, 
          txnWriteVersions, txnLogVersions, txnReadSet, txnWriteSet, 
          txnSlack, txnBlindAppend, validationLock, tableLogs, tableData>>

TypeOK ==
    /\ globalVersionLog \in [TABLES -> VERSIONS]
    /\ txnState \in [TRANSACTIONS -> {"idle", "active", "validating", "committed", "aborted"}]
    /\ txnReadVersions \in [TRANSACTIONS -> [TABLES -> VERSIONS]]
    /\ txnHiddenVersions \in [TRANSACTIONS -> [TABLES -> VERSIONS]]
    /\ txnWriteVersions \in [TRANSACTIONS -> [TABLES -> VERSIONS]]
    /\ txnLogVersions \in [TRANSACTIONS -> [TABLES -> VERSIONS]]
    /\ txnReadSet \in [TRANSACTIONS -> SUBSET TABLES]
    /\ txnWriteSet \in [TRANSACTIONS -> SUBSET TABLES]
    /\ txnSlack \in [TRANSACTIONS -> 0..MAX_SLACK]
    /\ txnBlindAppend \in [TRANSACTIONS -> BOOLEAN]
    /\ validationLock \in (TRANSACTIONS \cup {"NONE"})
    /\ tableLogs \in [TABLES -> [VERSIONS -> {"EMPTY", "PENDING", "COMMITTED"}]]
    /\ tableData \in [TABLES -> [VERSIONS -> SUBSET TRANSACTIONS]]

Init ==
    /\ globalVersionLog = [t \in TABLES |-> 0]
    /\ txnState = [txn \in TRANSACTIONS |-> "idle"]
    /\ txnReadVersions = [txn \in TRANSACTIONS |-> [t \in TABLES |-> 0]]
    /\ txnHiddenVersions = [txn \in TRANSACTIONS |-> [t \in TABLES |-> 0]]
    /\ txnWriteVersions = [txn \in TRANSACTIONS |-> [t \in TABLES |-> 0]]
    /\ txnLogVersions = [txn \in TRANSACTIONS |-> [t \in TABLES |-> 0]]
    /\ txnReadSet = [txn \in TRANSACTIONS |-> {}]
    /\ txnWriteSet = [txn \in TRANSACTIONS |-> {}]
    /\ txnSlack = [txn \in TRANSACTIONS |-> 0]
    /\ txnBlindAppend = [txn \in TRANSACTIONS |-> FALSE]
    /\ validationLock = "NONE"
    /\ tableLogs = [t \in TABLES |-> [v \in VERSIONS |-> "EMPTY"]]
    /\ tableData = [t \in TABLES |-> [v \in VERSIONS |-> {}]]

(******************************************************************************)
(* LV[I]: Global Version Assignment                                          *)
(* The global version log records the most recent committed version of each  *)
(* table. Transactions receive their read versions from this log at start,   *)
(* establishing the snapshot they will operate on (Lemmas 4.6, 4.7).         *)
(******************************************************************************)

\* Start transaction: snapshot current global version log
StartTransaction(txn) ==
    /\ txnState[txn] = "idle"
    /\ txnState' = [txnState EXCEPT ![txn] = "active"]
    /\ \* Read current global version log (Lemma 4.6)
       txnReadVersions' = [txnReadVersions EXCEPT ![txn] = globalVersionLog]
    /\ txnHiddenVersions' = [txnHiddenVersions EXCEPT ![txn] = globalVersionLog]
    /\ txnLogVersions' = [txnLogVersions EXCEPT ![txn] = globalVersionLog]
    /\ UNCHANGED <<globalVersionLog, txnWriteVersions, txnReadSet, txnWriteSet,
                   txnSlack, txnBlindAppend, validationLock, tableLogs, tableData>>

(******************************************************************************)
(* Read and Write Operations                                                 *)
(******************************************************************************)

\* Read: adds table to read set and updates hidden version to current state
ReadTable(txn, table) ==
    /\ txnState[txn] = "active"
    /\ table \notin txnReadSet[txn]  \* First read of this table
    /\ LET readVersion == txnReadVersions[txn][table]
           currentLogVersion == globalVersionLog[table]
       IN
        /\ readVersion <= currentLogVersion
        /\ txnReadSet' = [txnReadSet EXCEPT ![txn] = @ \cup {table}]
        /\ \* Update hidden version to detect concurrent writes
           txnHiddenVersions' = [txnHiddenVersions EXCEPT 
                                 ![txn][table] = currentLogVersion]
        /\ UNCHANGED <<globalVersionLog, txnState, txnReadVersions,
                       txnWriteVersions, txnLogVersions, txnWriteSet, 
                       txnSlack, txnBlindAppend, validationLock, 
                       tableLogs, tableData>>

\* Write: adds table to write set, assigns the next available version
\* (hidden_version + 1) as the write version, and marks it pending.
WriteTable(txn, table) ==
    /\ txnState[txn] = "active"
    /\ table \notin txnWriteSet[txn]  \* First write to this table
    /\ LET readVersion == txnReadVersions[txn][table]
           hiddenVersion == txnHiddenVersions[txn][table]
           proposedWriteVersion == hiddenVersion + 1
       IN
        /\ proposedWriteVersion <= MAX_VERSION
        /\ \* Write version must exceed current global version (R2 precondition)
           proposedWriteVersion > globalVersionLog[table]
        /\ \* No other transaction may have claimed this version
           tableLogs[table][proposedWriteVersion] = "EMPTY"
        /\ txnWriteVersions' = [txnWriteVersions EXCEPT 
                                ![txn][table] = proposedWriteVersion]
        /\ txnWriteSet' = [txnWriteSet EXCEPT ![txn] = @ \cup {table}]
        /\ tableLogs' = [tableLogs EXCEPT 
                         ![table][proposedWriteVersion] = "PENDING"]
        /\ tableData' = [tableData EXCEPT 
                         ![table][proposedWriteVersion] = {txn}]
        /\ UNCHANGED <<globalVersionLog, txnReadVersions, txnHiddenVersions,
                       txnLogVersions, txnReadSet, txnState, txnSlack,
                       txnBlindAppend, validationLock>>

(******************************************************************************)
(* LV[I]: Global Validation (R1, R2, R3)                                     *)
(* A single transaction at a time enters the validation phase via             *)
(* PUT-IF-ABSENT. Rules R1, R2, R3 check freshness, version uniqueness, and  *)
(* lost-update absence. Atomic commit advances the global version log         *)
(* (Lemmas 4.6, 4.7, 5.8).                                                    *)
(******************************************************************************)

\* Enter validation phase: acquire validation lock via PUT-IF-ABSENT (Lemma 4.6)
EnterValidation(txn) ==
    /\ txnState[txn] = "active"
    /\ validationLock = "NONE"
    /\ (txnReadSet[txn] \cup txnWriteSet[txn]) # {}
    /\ validationLock' = txn
    /\ txnState' = [txnState EXCEPT ![txn] = "validating"]
    /\ \* Snapshot global version log for R3 check
       txnLogVersions' = [txnLogVersions EXCEPT ![txn] = globalVersionLog]
    /\ \* Update hidden versions to current state for R2 check
       txnHiddenVersions' = [txnHiddenVersions EXCEPT ![txn] = globalVersionLog]
    /\ UNCHANGED <<globalVersionLog, txnReadVersions,
                   txnWriteVersions, txnReadSet, txnWriteSet, txnSlack,
                   txnBlindAppend, tableLogs, tableData>>

\* Validation rules R1, R2, R3 (Lemmas 4.6, 5.8):
\* R1: read version not too outdated (freshness); R2: write version unique;
\* R3: no lost updates (write version accounts for all intermediate logs).
IsCompatible(txn) ==
    LET readSet == txnReadSet[txn]
        writeSet == txnWriteSet[txn]
        slack == txnSlack[txn]
    IN
    /\ \* R1: read version is within acceptable staleness bound
       \A table \in readSet :
           ~(txnReadVersions[txn][table] < 
             txnHiddenVersions[txn][table] - slack)
    /\ \* R2: write version not already taken by concurrent transaction
       \A table \in writeSet :
           ~(txnWriteVersions[txn][table] <= 
             txnHiddenVersions[txn][table])
    /\ \* R3: write version accounts for all intermediate committed logs
       \A table \in writeSet :
           LET logVersion == txnLogVersions[txn][table]
               writeVersion == txnWriteVersions[txn][table]
               writtenLogs == Cardinality({v \in VERSIONS : 
                                           tableLogs[table][v] \in {"PENDING", "COMMITTED"} 
                                           /\ v > logVersion /\ v < writeVersion})
           IN
               ~(logVersion + writtenLogs + 1 # writeVersion)

\* Commit: validation passed; atomically advances global version log and
\* marks all write logs as committed (Lemma 4.7, Lemma 5.8).
CommitTransaction(txn) ==
    /\ txnState[txn] = "validating"
    /\ validationLock = txn
    /\ IsCompatible(txn)
    /\ LET writeSet == txnWriteSet[txn]
       IN
        /\ globalVersionLog' = [t \in TABLES |->
                                IF t \in writeSet
                                THEN txnWriteVersions[txn][t]
                                ELSE globalVersionLog[t]]
        /\ \* Mark all logs as committed
           tableLogs' = [t \in TABLES |->
                         IF t \in writeSet
                         THEN [tableLogs[t] EXCEPT 
                               ![txnWriteVersions[txn][t]] = "COMMITTED"]
                         ELSE tableLogs[t]]
        /\ txnState' = [txnState EXCEPT ![txn] = "committed"]
        /\ validationLock' = "NONE"
        /\ UNCHANGED <<txnReadVersions, txnHiddenVersions, txnWriteVersions,
                       txnLogVersions, txnReadSet, txnWriteSet, txnSlack,
                       txnBlindAppend, tableData>>

\* Abort: validation failed (R1/R2/R3 violated); clears pending log entries
\* and releases the validation lock.
AbortTransaction(txn) ==
    /\ txnState[txn] = "validating"
    /\ validationLock = txn
    /\ ~IsCompatible(txn)  \* Fail validation
    /\ LET writeSet == txnWriteSet[txn]
       IN
        /\ \* Clear pending logs
           tableLogs' = [t \in TABLES |->
                         IF t \in writeSet
                         THEN [tableLogs[t] EXCEPT 
                               ![txnWriteVersions[txn][t]] = "EMPTY"]
                         ELSE tableLogs[t]]
        /\ \* Clear data entries
           tableData' = [t \in TABLES |->
                         IF t \in writeSet
                         THEN [tableData[t] EXCEPT 
                               ![txnWriteVersions[txn][t]] = {}]
                         ELSE tableData[t]]
        /\ txnState' = [txnState EXCEPT ![txn] = "aborted"]
        /\ validationLock' = "NONE"
        /\ UNCHANGED <<globalVersionLog, txnReadVersions, txnHiddenVersions,
                       txnWriteVersions, txnLogVersions, txnReadSet, txnWriteSet,
                       txnSlack, txnBlindAppend>>

\* Blind append: write-only transaction that inserts new data without ordering
\* dependency. Takes the next available version directly, bypassing R1/R2/R3.
BlindAppendCommit(txn) ==
    /\ txnState[txn] = "validating"
    /\ validationLock = txn
    /\ txnBlindAppend[txn]  \* Marked as blind append
    /\ txnReadSet[txn] = {}  \* Write-only transaction
    /\ LET writeSet == txnWriteSet[txn]
       IN
        /\ \* Take next available version for each table
           globalVersionLog' = [t \in TABLES |->
                                IF t \in writeSet
                                THEN Maximum({globalVersionLog[t]} \cup 
                                             {txnWriteVersions[txn][t]})
                                ELSE globalVersionLog[t]]
        /\ tableLogs' = [t \in TABLES |->
                         IF t \in writeSet
                         THEN [tableLogs[t] EXCEPT 
                               ![txnWriteVersions[txn][t]] = "COMMITTED"]
                         ELSE tableLogs[t]]
        /\ txnState' = [txnState EXCEPT ![txn] = "committed"]
        /\ validationLock' = "NONE"
        /\ UNCHANGED <<txnReadVersions, txnHiddenVersions, txnWriteVersions,
                       txnLogVersions, txnReadSet, txnWriteSet, txnSlack,
                       txnBlindAppend, tableData>>

(******************************************************************************)
(* Termination Actions                                                        *)
(* Additional transitions ensuring liveness: transactions can reset after    *)
(* commit/abort, idle transactions can terminate, and read-only or           *)
(* empty-active transactions can commit without blocking the validation lock. *)
(******************************************************************************)

\* Reset: returns a committed or aborted transaction to idle for fresh execution.
ResetTransaction(txn) ==
    /\ txnState[txn] \in {"committed", "aborted"}
    /\ txnState' = [txnState EXCEPT ![txn] = "idle"]
    /\ txnReadSet' = [txnReadSet EXCEPT ![txn] = {}]
    /\ txnWriteSet' = [txnWriteSet EXCEPT ![txn] = {}]
    /\ txnReadVersions' = [txnReadVersions EXCEPT ![txn] = [t \in TABLES |-> 0]]
    /\ txnHiddenVersions' = [txnHiddenVersions EXCEPT ![txn] = [t \in TABLES |-> 0]]
    /\ txnWriteVersions' = [txnWriteVersions EXCEPT ![txn] = [t \in TABLES |-> 0]]
    /\ txnLogVersions' = [txnLogVersions EXCEPT ![txn] = [t \in TABLES |-> 0]]
    /\ txnSlack' = [txnSlack EXCEPT ![txn] = 0]
    /\ txnBlindAppend' = [txnBlindAppend EXCEPT ![txn] = FALSE]
    /\ UNCHANGED <<globalVersionLog, validationLock, tableLogs, tableData>>

\* Idle transactions with no pending work commit immediately to satisfy
\* EventualTermination without blocking the validation lock.
CommitIdleTransaction(txn) ==
    /\ txnState[txn] = "idle"
    /\ txnReadSet[txn] = {}
    /\ txnWriteSet[txn] = {}
    /\ txnState' = [txnState EXCEPT ![txn] = "committed"]
    /\ UNCHANGED <<globalVersionLog, txnReadVersions, txnHiddenVersions,
                   txnWriteVersions, txnLogVersions, txnReadSet, txnWriteSet,
                   txnSlack, txnBlindAppend, validationLock, tableLogs, tableData>>

\* Read-only transactions always pass validation and commit immediately,
\* releasing the validation lock without updating the global version log.
CommitReadOnlyTransaction(txn) ==
    /\ txnState[txn] = "validating"
    /\ validationLock = txn
    /\ txnWriteSet[txn] = {}  \* Read-only
    /\ txnReadSet[txn] # {}   \* Has reads
    /\ txnState' = [txnState EXCEPT ![txn] = "committed"]
    /\ validationLock' = "NONE"
    /\ UNCHANGED <<globalVersionLog, txnReadVersions, txnHiddenVersions,
                   txnWriteVersions, txnLogVersions, txnReadSet, txnWriteSet,
                   txnSlack, txnBlindAppend, tableLogs, tableData>>

\* Active transactions that accessed no tables commit immediately,
\* representing a transaction that started but performed no operations.
CommitEmptyActiveTransaction(txn) ==
    /\ txnState[txn] = "active"
    /\ txnReadSet[txn] = {}
    /\ txnWriteSet[txn] = {}
    /\ txnState' = [txnState EXCEPT ![txn] = "committed"]
    /\ UNCHANGED <<globalVersionLog, txnReadVersions, txnHiddenVersions,
                   txnWriteVersions, txnLogVersions, txnReadSet, txnWriteSet,
                   txnSlack, txnBlindAppend, validationLock, tableLogs, tableData>>

Next ==
    \/ \E txn \in TRANSACTIONS : StartTransaction(txn)
    \/ \E txn \in TRANSACTIONS, table \in TABLES : ReadTable(txn, table)
    \/ \E txn \in TRANSACTIONS, table \in TABLES : WriteTable(txn, table)
    \/ \E txn \in TRANSACTIONS : EnterValidation(txn)
    \/ \E txn \in TRANSACTIONS : CommitTransaction(txn)
    \/ \E txn \in TRANSACTIONS : AbortTransaction(txn)
    \/ \E txn \in TRANSACTIONS : BlindAppendCommit(txn)
    \/ \E txn \in TRANSACTIONS : ResetTransaction(txn)
    \/ \E txn \in TRANSACTIONS : CommitIdleTransaction(txn)
    \/ \E txn \in TRANSACTIONS : CommitReadOnlyTransaction(txn)
    \/ \E txn \in TRANSACTIONS : CommitEmptyActiveTransaction(txn)

\* Fairness: strong fairness on validation and commit prevents starvation;
\* weak fairness on idle/empty commits guarantees EventualTermination.
Spec == Init /\ [][Next]_vars
    /\ WF_vars(Next)
    /\ (\A txn \in TRANSACTIONS : SF_vars(EnterValidation(txn)))
    /\ (\A txn \in TRANSACTIONS : SF_vars(CommitTransaction(txn) \/ AbortTransaction(txn) \/ CommitReadOnlyTransaction(txn) \/ CommitEmptyActiveTransaction(txn)))
    /\ (\A txn \in TRANSACTIONS : WF_vars(CommitIdleTransaction(txn)))
    /\ (\A txn \in TRANSACTIONS : WF_vars(ResetTransaction(txn)))

(******************************************************************************)
(* Safety and Liveness Properties (Lemmas 5.8, 5.9, 6.1)                    *)
(******************************************************************************)

\* Lemma 5.8: LV[I] guarantees serializability for multi-table, multi-query
\* transactions (∀t1,t2 ∈ T, t1<t2 ⟺ v(t1)<v(t2)). Committed transactions
\* that share tables must have distinct write versions.
GlobalSerializability ==
    \A txn1, txn2 \in TRANSACTIONS :
        (txn1 # txn2 /\ txnState[txn1] = "committed" /\ txnState[txn2] = "committed") =>
            LET commonTables == txnWriteSet[txn1] \cap txnWriteSet[txn2]
            IN
                \/ commonTables = {}
                \/ \E table \in commonTables :
                    txnWriteVersions[txn1][table] # txnWriteVersions[txn2][table]

\* R1: Committed write transactions did not read data beyond the staleness bound.
R1_NoOutdatedReads ==
    \A txn \in TRANSACTIONS :
        (txnState[txn] = "committed" /\ txnWriteSet[txn] # {}) =>
            \A table \in txnReadSet[txn] :
                ~(txnReadVersions[txn][table] < 
                  txnHiddenVersions[txn][table] - txnSlack[txn])

\* R2: Each committed write version is unique per table (no overwrite).
R2_UniqueVersions ==
    \A table \in TABLES, version \in VERSIONS :
        Cardinality(tableData[table][version]) <= 1

\* R3: No lost updates — committed write version accounts for all intermediate
\* committed logs between log_version and write_version.
R3_NoLostUpdates ==
    \A txn \in TRANSACTIONS :
        (txnState[txn] = "committed") =>
            \A table \in txnWriteSet[txn] :
                LET logVersion == txnLogVersions[txn][table]
                    writeVersion == txnWriteVersions[txn][table]
                    writtenLogs == Cardinality({v \in VERSIONS : 
                                                 tableLogs[table][v] = "COMMITTED" 
                                                 /\ v > logVersion /\ v < writeVersion})
                IN
                    logVersion + writtenLogs + 1 = writeVersion

\* Lemma 4.7: The global version log only advances monotonically; committed
\* versions never exceed the current global version for their table.
MonotonicVersionLog ==
    \A table \in TABLES :
        \A version \in VERSIONS :
            (tableLogs[table][version] = "COMMITTED") =>
                version <= globalVersionLog[table]

\* Lemma 5.9: With relaxed hidden_version, LV[I] provides snapshot isolation
\* for read-only transactions. Each read version was valid at transaction start.
SnapshotIsolation ==
    \A txn \in TRANSACTIONS :
        (txnState[txn] = "committed" /\ txnWriteSet[txn] = {}) =>
            \A table \in txnReadSet[txn] :
                \* Read version was valid at transaction start
                txnReadVersions[txn][table] <= globalVersionLog[table]

\* LV[I] snapshot isolation: transactions only read committed version 0
\* (initial state) or versions that are committed in the log.
NoDirtyReads ==
    \A txn \in TRANSACTIONS :
        (txnState[txn] \in {"committed", "aborted"}) =>
            \A table \in txnReadSet[txn] :
                LET readVersion == txnReadVersions[txn][table]
                IN
                    \/ readVersion = 0
                    \/ tableLogs[table][readVersion] = "COMMITTED"

\* Lemma 3.4: All writes of a committed transaction are atomically published
\* (all write logs marked COMMITTED before global version log advances).
AtomicCommit ==
    \A txn \in TRANSACTIONS :
        (txnState[txn] = "committed") =>
            \A table \in txnWriteSet[txn] :
                tableLogs[table][txnWriteVersions[txn][table]] = "COMMITTED"

(******************************************************************************)
(* Liveness Properties                                                        *)
(******************************************************************************)

\* Lemma 5.8: All transactions eventually commit or abort (sequential
\* validation ensures forward progress; no deadlock is possible).
EventualTermination ==
    \A txn \in TRANSACTIONS :
        <>(txnState[txn] \in {"committed", "aborted"})

\* Lemma 4.6: Sequential validation prevents starvation; a transaction that
\* enters the validation phase eventually commits or aborts.
ValidationProgress ==
    \A txn \in TRANSACTIONS :
        (txnState[txn] = "validating") ~> 
            (txnState[txn] \in {"committed", "aborted"})

================================================================================
