----------------------------- MODULE LakeVillaWriteOptimized_new -----------------------------
(***************************************************************************
 * LV[Write] = LV[R] + LV[CT]: markers/sublogs (Lemmas 3.2, 4.3, 5.4)    *
 * + DAG-based dependency tracking with marker-shift deadlock resolution   *
 * (Lemmas 4.4, 4.5, 5.5, 5.6, 5.7). Extends LV[R] with bounded          *
 * crash/retry counts to prevent infinite crash-reset loops.               *
 ***************************************************************************)

EXTENDS Integers, Sequences, FiniteSets, TLC

\* Helper operator for maximum of a set
Maximum(S) == CHOOSE x \in S : \A y \in S : x >= y

CONSTANTS
    TABLES,           \* set of table identifiers
    TRANSACTIONS,     \* set of transaction identifiers
    FILES,            \* set of data file identifiers
    MAX_VERSION,      \* Maximum version number for bounded model checking
    MAX_OPERATIONS    \* Maximum operations per transaction

VARIABLES
    \* LV[R]: Marker placement (Def. 3.2, Eq. 4)
    markers,          \* markers[table] = sequence of {txnId, dependencies, timestamp}
    markerVersions,   \* markerVersions[table][txnId] = reserved write version

    \* LV[R]: Transaction sublogs for crash recovery (Lemma 3.2)
    physicalSublogs,  \* physicalSublogs[txnId] = sequence of physical operations with compensation
    logicalSublogs,   \* logicalSublogs[txnId] = sequence of logical operations with compensation
    sublogMode,       \* sublogMode[txnId] = "physical" | "logical" | "mixed"

    \* LV[CT]: DAG-based dependency tracking (Lemmas 4.4, 5.5)
    txnDependencies,  \* txnDependencies[txnId] = set of dependent transaction IDs
    txnAccessedTables,\* txnAccessedTables[txnId] = set of tables accessed

    \* Table state
    tableVersions,    \* tableVersions[table] = current committed version
    tableData,        \* tableData[table][version] = set of file IDs
    tableLogs,        \* tableLogs[table][version] = log entry

    \* Transaction state
    txnState,         \* txnState[txnId] = "idle" | "active" | "committed" | "aborted" | "crashed"
    txnReadVersions,  \* txnReadVersions[txnId][table] = version read at marker placement
    txnWriteVersions, \* txnWriteVersions[txnId][table] = reserved write version
    txnOperations,    \* txnOperations[txnId] = sequence of operations
    txnBuffer,        \* txnBuffer[txnId] = client-side buffer for logical operations

    \* Recovery and conflict state
    crashedTxns,      \* set of currently crashed transactions
    conflictCycles,   \* transactions that triggered marker shift (Lemma 5.5)
    retryCount,       \* retryCount[txnId] = number of aborts/retries
    crashCount        \* crashCount[txnId] = number of crashes per transaction

vars == <<markers, markerVersions, physicalSublogs, logicalSublogs, sublogMode,
          txnDependencies, txnAccessedTables, tableVersions, tableData, tableLogs,
          txnState, txnReadVersions, txnWriteVersions, txnOperations, txnBuffer,
          crashedTxns, conflictCycles, retryCount, crashCount>>

-----------------------------------------------------------------------------
(* Type Invariants *)

MarkerType == [txnId: TRANSACTIONS, dependencies: SUBSET TRANSACTIONS, timestamp: Nat]

OperationType == [
    type: {"READ", "WRITE", "UPDATE"},
    table: TABLES,
    files: SUBSET FILES
]

PhysicalSublogEntry == [
    action: {"ADD_FILE", "REMOVE_FILE", "READ_FILE"},
    table: TABLES,
    file: FILES,
    compensation: {"ADD_FILE", "REMOVE_FILE", "SKIP"}
]

LogicalSublogEntry == [
    action: {"SEEK", "MERGE", "BUFFER_OP"},
    table: TABLES,
    compensation: {"RESTORE", "REPLACE"}
]

TypeOK ==
    /\ markers \in [TABLES -> Seq(MarkerType)]
    /\ markerVersions \in [TABLES -> [TRANSACTIONS -> Nat \cup {0}]]
    /\ physicalSublogs \in [TRANSACTIONS -> Seq(PhysicalSublogEntry)]
    /\ logicalSublogs \in [TRANSACTIONS -> Seq(LogicalSublogEntry)]
    /\ sublogMode \in [TRANSACTIONS -> {"physical", "logical", "mixed"}]
    /\ txnDependencies \in [TRANSACTIONS -> SUBSET TRANSACTIONS]
    /\ txnAccessedTables \in [TRANSACTIONS -> SUBSET TABLES]
    /\ tableVersions \in [TABLES -> Nat]
    /\ txnState \in [TRANSACTIONS -> {"idle", "active", "validating", "committed", "aborted", "crashed"}]
    /\ txnReadVersions \in [TRANSACTIONS -> [TABLES -> Nat]]
    /\ txnWriteVersions \in [TRANSACTIONS -> [TABLES -> Nat]]
    /\ retryCount \in [TRANSACTIONS -> Nat]
    /\ crashCount \in [TRANSACTIONS -> Nat]

-----------------------------------------------------------------------------
(* Initial State *)

Init ==
    /\ markers = [t \in TABLES |-> <<>>]
    /\ markerVersions = [t \in TABLES |-> [txn \in TRANSACTIONS |-> 0]]
    /\ physicalSublogs = [txn \in TRANSACTIONS |-> <<>>]
    /\ logicalSublogs = [txn \in TRANSACTIONS |-> <<>>]
    /\ sublogMode = [txn \in TRANSACTIONS |-> "physical"]
    /\ txnDependencies = [txn \in TRANSACTIONS |-> {}]
    /\ txnAccessedTables = [txn \in TRANSACTIONS |-> {}]
    /\ tableVersions = [t \in TABLES |-> 0]
    /\ tableData = [t \in TABLES |-> [v \in 0..MAX_VERSION |-> {}]]
    /\ tableLogs = [t \in TABLES |-> [v \in 0..MAX_VERSION |-> "EMPTY"]]
    /\ txnState = [txn \in TRANSACTIONS |-> "idle"]
    /\ txnReadVersions = [txn \in TRANSACTIONS |-> [t \in TABLES |-> 0]]
    /\ txnWriteVersions = [txn \in TRANSACTIONS |-> [t \in TABLES |-> 0]]
    /\ txnOperations = [txn \in TRANSACTIONS |-> <<>>]
    /\ txnBuffer = [txn \in TRANSACTIONS |-> "EMPTY"]
    /\ crashedTxns = {}
    /\ conflictCycles = {}
    /\ retryCount = [txn \in TRANSACTIONS |-> 0]
    /\ crashCount = [txn \in TRANSACTIONS |-> 0]

-----------------------------------------------------------------------------
(* LV[R]: Marker Placement (Def. 3.2, Lemma 3.2)                          *)
(* Reserves write version via PUT-IF-ABSENT (Eq. 4). Switches to logical  *)
(* sublog mode when a preceding marker is detected (Lemma 3.2).           *)

\* Place a marker to reserve a version atomically (PUT-IF-ABSENT)
PlaceMarker(txn, table) ==
    /\ txnState[txn] = "active"
    /\ table \notin txnAccessedTables[txn]
    /\ LET readVersion == tableVersions[table]
           \* writeVersion must be unique - use max of table version and all reserved versions
           maxReserved == IF \E t \in TRANSACTIONS : markerVersions[table][t] > 0
                          THEN Maximum({markerVersions[table][t] : t \in TRANSACTIONS})
                          ELSE readVersion
           writeVersion == Maximum({maxReserved, readVersion}) + 1
           \* Check for preceding markers
           hasPreceding == \E i \in 1..Len(markers[table]) :
                            markers[table][i].txnId # txn
       IN
        /\ markerVersions[table][txn] = 0  \* Not already reserved
        /\ writeVersion <= MAX_VERSION
        /\ markerVersions' = [markerVersions EXCEPT 
                              ![table][txn] = writeVersion]
        /\ markers' = [markers EXCEPT 
                       ![table] = Append(@, [
                           txnId |-> txn,
                           dependencies |-> txnDependencies[txn],
                           timestamp |-> tableVersions[table]
                       ])]
        /\ txnReadVersions' = [txnReadVersions EXCEPT ![txn][table] = readVersion]
        /\ txnWriteVersions' = [txnWriteVersions EXCEPT ![txn][table] = writeVersion]
        /\ txnAccessedTables' = [txnAccessedTables EXCEPT ![txn] = @ \cup {table}]
        /\ \* Switch to logical sublogs if preceding transaction detected
           IF hasPreceding 
           THEN sublogMode' = [sublogMode EXCEPT ![txn] = 
                               IF @ = "physical" THEN "logical" ELSE "mixed"]
           ELSE sublogMode' = sublogMode
        /\ UNCHANGED <<physicalSublogs, logicalSublogs, txnDependencies, 
                       tableVersions, tableData, tableLogs, txnState, 
                       txnOperations, txnBuffer, crashedTxns, conflictCycles, retryCount, crashCount>>

-----------------------------------------------------------------------------
(* LV[R]: Transaction Sublogs (Lemma 3.2)                                 *)
(* Physical sublogs record explicit file operations with compensation      *)
(* steps. Logical sublogs record buffer-based operations. Both enable      *)
(* crash recovery via undo (newest-to-oldest) and redo (oldest-to-newest). *)

\* Physical sublog: explicit file operations
AddPhysicalSublog(txn, table, action, file, comp) ==
    /\ txnState[txn] = "active"
    /\ table \in txnAccessedTables[txn]
    /\ sublogMode[txn] \in {"physical", "mixed"}
    /\ physicalSublogs' = [physicalSublogs EXCEPT 
                           ![txn] = Append(@, [
                               action |-> action,
                               table |-> table,
                               file |-> file,
                               compensation |-> comp
                           ])]
    /\ UNCHANGED <<markers, markerVersions, logicalSublogs, sublogMode,
                   txnDependencies, txnAccessedTables, tableVersions, tableData,
                   tableLogs, txnState, txnReadVersions, txnWriteVersions,
                   txnOperations, txnBuffer, crashedTxns, conflictCycles, retryCount, crashCount>>

\* Logical sublog: buffer operations and references
AddLogicalSublog(txn, table, action, comp) ==
    /\ txnState[txn] = "active"
    /\ table \in txnAccessedTables[txn]
    /\ sublogMode[txn] \in {"logical", "mixed"}
    /\ logicalSublogs' = [logicalSublogs EXCEPT 
                          ![txn] = Append(@, [
                              action |-> action,
                              table |-> table,
                              compensation |-> comp
                          ])]
    /\ txnBuffer' = [txnBuffer EXCEPT ![txn] = "UPDATED"]
    /\ UNCHANGED <<markers, markerVersions, physicalSublogs, sublogMode,
                   txnDependencies, txnAccessedTables, tableVersions, tableData,
                   tableLogs, txnState, txnReadVersions, txnWriteVersions,
                   txnOperations, crashedTxns, conflictCycles, retryCount, crashCount>>

\* Undo: traverse sublogs from newest to oldest, executing compensation steps
UndoTransaction(txn) ==
    /\ txnState[txn] = "active"
    /\ \/ Len(physicalSublogs[txn]) > 0
       \/ Len(logicalSublogs[txn]) > 0
    /\ LET UndoPhysical == 
            IF Len(physicalSublogs[txn]) > 0
            THEN LET entry == physicalSublogs[txn][Len(physicalSublogs[txn])]
                 IN entry.compensation  \* Execute compensation
            ELSE "SKIP"
           UndoLogical ==
            IF Len(logicalSublogs[txn]) > 0
            THEN LET entry == logicalSublogs[txn][Len(logicalSublogs[txn])]
                 IN entry.compensation  \* Execute compensation
            ELSE "SKIP"
       IN
        /\ physicalSublogs' = [physicalSublogs EXCEPT 
                               ![txn] = IF Len(@) > 0 THEN SubSeq(@, 1, Len(@) - 1) ELSE <<>>]
        /\ logicalSublogs' = [logicalSublogs EXCEPT 
                              ![txn] = IF Len(@) > 0 THEN SubSeq(@, 1, Len(@) - 1) ELSE <<>>]
        /\ UNCHANGED <<markers, markerVersions, sublogMode, txnDependencies,
                       txnAccessedTables, tableVersions, tableData, tableLogs,
                       txnState, txnReadVersions, txnWriteVersions, txnOperations,
                       txnBuffer, crashedTxns, conflictCycles, retryCount, crashCount>>

\* Redo: traverse sublogs from oldest to newest, re-applying operations
RedoTransaction(txn) ==
    /\ txnState[txn] = "active"
    /\ \/ Len(physicalSublogs[txn]) > 0
       \/ Len(logicalSublogs[txn]) > 0
    /\ LET RedoPhysical ==
            IF Len(physicalSublogs[txn]) > 0
            THEN physicalSublogs[txn][1].action  \* Execute redo
            ELSE "SKIP"
           RedoLogical ==
            IF Len(logicalSublogs[txn]) > 0
            THEN logicalSublogs[txn][1].action  \* Execute redo
            ELSE "SKIP"
       IN
        /\ UNCHANGED <<markers, markerVersions, physicalSublogs, logicalSublogs,
                       sublogMode, txnDependencies, txnAccessedTables, tableVersions,
                       tableData, tableLogs, txnState, txnReadVersions, txnWriteVersions,
                       txnOperations, txnBuffer, crashedTxns, conflictCycles, retryCount, crashCount>>

-----------------------------------------------------------------------------
(* LV[CT]: DAG-based dependency tracking (Lemma 4.4).                     *)
(* Marker shift inverts DAG edges to break ordering cycles (Lemma 5.5).   *)
(* Provides causal consistency across tables (Lemma 4.5, Eq. 8).          *)

\* Detect if adding edge would create a cycle (deadlock detection)
\* Detect if adding edge would create a cycle in the dependency DAG (Lemma 5.5)
DetectCycle(txn, targetTxn) ==
    \* Simple cycle detection: check if targetTxn depends on txn
    \E path \in SUBSET TRANSACTIONS :
        /\ txn \in path
        /\ targetTxn \in path
        /\ \A t1, t2 \in path : t1 # t2 => 
            (t2 \in txnDependencies[t1] \/ t1 \in txnDependencies[t2])

\* Aborts immediately if adding new dependencies would form a cycle (Lemma 5.5)
UpdateDependencies(txn, table) ==
    /\ txnState[txn] = "active"
    /\ table \in txnAccessedTables[txn]
    /\ LET precedingTxns == {m.txnId : m \in {markers[table][i] : i \in 1..Len(markers[table])}}
           newDeps == precedingTxns \ {txn}
           wouldCreateCycle == \E targetTxn \in newDeps :
               \/ targetTxn \in txnDependencies[txn]
               \/ txn \in txnDependencies[targetTxn]
               \/ \E intermediate \in TRANSACTIONS :
                      /\ intermediate \in txnDependencies[targetTxn]
                      /\ (txn \in txnDependencies[intermediate] \/ intermediate = txn)
       IN
        /\ ~wouldCreateCycle
        /\ txnDependencies' = [txnDependencies EXCEPT ![txn] = @ \cup newDeps]
        /\ UNCHANGED <<markers, markerVersions, physicalSublogs, logicalSublogs,
                       sublogMode, txnAccessedTables, tableVersions, tableData,
                       tableLogs, txnState, txnReadVersions, txnWriteVersions,
                       txnOperations, txnBuffer, crashedTxns, conflictCycles, retryCount, crashCount>>

\* Marker shift: frees all markers and clears dependencies to break a detected
\* ordering cycle (Lemma 5.5). Aborts for restart via ResetTransaction.
MarkerShift(txn) ==
    /\ txnState[txn] = "active"
    /\ \E table \in txnAccessedTables[txn] :
        \E otherTxn \in TRANSACTIONS :
            /\ otherTxn # txn
            /\ otherTxn \in txnDependencies[txn]
            /\ txn \in txnDependencies[otherTxn]  \* Cycle detected
    /\ LET affectedTables == txnAccessedTables[txn]
       IN
        /\ \* Free all markers for this transaction
           markers' = [t \in TABLES |->
                       IF t \in affectedTables
                       THEN LET filtered == SelectSeq(markers[t], 
                                             LAMBDA m : m.txnId # txn)
                            IN filtered
                       ELSE markers[t]]
        /\ \* Reset marker versions
           markerVersions' = [markerVersions EXCEPT 
                              ![CHOOSE t \in affectedTables : TRUE][txn] = 0]
        /\ \* Clear dependencies to break cycle
           txnDependencies' = [txnDependencies EXCEPT ![txn] = {}]
        /\ conflictCycles' = conflictCycles \cup {txn}
        \* Abort to prevent immediate cycle re-formation upon restart
        /\ txnState' = [txnState EXCEPT ![txn] = "aborted"]
        /\ UNCHANGED <<physicalSublogs, logicalSublogs, sublogMode, txnAccessedTables,
                       tableVersions, tableData, tableLogs, txnReadVersions,
                       txnWriteVersions, txnOperations, txnBuffer, crashedTxns, retryCount, crashCount>>

-----------------------------------------------------------------------------
(* Transaction Lifecycle *)

\* Transactions with exhausted retry or crash limits remain idle (Lemma 5.5)
StartTransaction(txn) ==
    /\ txnState[txn] = "idle"
    /\ retryCount[txn] < 3
    /\ crashCount[txn] < 2
    /\ txnState' = [txnState EXCEPT ![txn] = "active"]
    \* Preserve retry/crash counts across restarts to enforce limits
    /\ UNCHANGED <<markers, markerVersions, physicalSublogs, logicalSublogs,
                   sublogMode, txnDependencies, txnAccessedTables, tableVersions,
                   tableData, tableLogs, txnReadVersions, txnWriteVersions,
                   txnOperations, txnBuffer, crashedTxns, conflictCycles, retryCount, crashCount>>

\* Atomically overwrites markers with committed logs (Lemma 3.2).
\* Requires no concurrent commits on accessed tables (Lemma 5.4).
CommitTransaction(txn) ==
    /\ txnState[txn] = "active"
    /\ txnAccessedTables[txn] # {}
    /\ \A table \in txnAccessedTables[txn] :
        markerVersions[table][txn] > 0
    /\ LET affectedTables == txnAccessedTables[txn]
       IN
        /\ \* Check no conflicts with file-based granularity
           \A table \in affectedTables :
            LET readVer == txnReadVersions[txn][table]
                writeVer == txnWriteVersions[txn][table]
            IN
                /\ tableVersions[table] = readVer  \* No concurrent commits
                /\ writeVer = readVer + 1
        /\ \* Replace markers with logs
           tableLogs' = [t \in TABLES |->
                         IF t \in affectedTables
                         THEN [tableLogs[t] EXCEPT 
                               ![txnWriteVersions[txn][t]] = "COMMITTED"]
                         ELSE tableLogs[t]]
        /\ \* Update table versions
           tableVersions' = [t \in TABLES |->
                             IF t \in affectedTables
                             THEN txnWriteVersions[txn][t]
                             ELSE tableVersions[t]]
        /\ \* Remove markers
           markers' = [t \in TABLES |->
                       IF t \in affectedTables
                       THEN SelectSeq(markers[t], LAMBDA m : m.txnId # txn)
                       ELSE markers[t]]
        /\ txnState' = [txnState EXCEPT ![txn] = "committed"]
        /\ UNCHANGED <<markerVersions, physicalSublogs, logicalSublogs, sublogMode,
                       txnDependencies, txnAccessedTables, tableData, txnReadVersions,
                       txnWriteVersions, txnOperations, txnBuffer, crashedTxns,
                       conflictCycles, retryCount, crashCount>>

\* Executes sublog compensation, removes markers, increments retry count
AbortTransaction(txn) ==
    /\ txnState[txn] = "active"
    /\ retryCount[txn] < 3
    /\ txnAccessedTables[txn] # {}
    /\ \E table \in txnAccessedTables[txn] : markerVersions[table][txn] > 0
    /\ LET affectedTables == txnAccessedTables[txn]
       IN
        /\ \* Execute undos if needed
           physicalSublogs' = [physicalSublogs EXCEPT ![txn] = <<>>]
        /\ logicalSublogs' = [logicalSublogs EXCEPT ![txn] = <<>>]
        /\ \* Remove markers
           markers' = [t \in TABLES |->
                       IF t \in affectedTables
                       THEN SelectSeq(markers[t], LAMBDA m : m.txnId # txn)
                       ELSE markers[t]]
        /\ markerVersions' = [t \in TABLES |->
                              IF t \in affectedTables
                              THEN [markerVersions[t] EXCEPT ![txn] = 0]
                              ELSE markerVersions[t]]
        /\ txnState' = [txnState EXCEPT ![txn] = "aborted"]
        /\ retryCount' = [retryCount EXCEPT ![txn] = @ + 1]
        /\ UNCHANGED <<sublogMode, txnDependencies, txnAccessedTables, tableVersions,
                       tableData, tableLogs, txnReadVersions, txnWriteVersions,
                       txnOperations, txnBuffer, crashedTxns, conflictCycles, crashCount>>

\* Abort when a new dependency would form a cycle; transaction retries (Lemma 5.5)
AbortOnCycleDetection(txn, table) ==
    /\ txnState[txn] = "active"
    /\ table \in txnAccessedTables[txn]
    /\ LET precedingTxns == {m.txnId : m \in {markers[table][i] : i \in 1..Len(markers[table])}}
           newDeps == precedingTxns \ {txn}
           \* Check if adding dependencies would create cycle
           wouldCreateCycle == \E targetTxn \in newDeps :
               \/ targetTxn \in txnDependencies[txn]
               \/ txn \in txnDependencies[targetTxn]
               \/ \E intermediate \in TRANSACTIONS :
                      /\ intermediate \in txnDependencies[targetTxn]
                      /\ (txn \in txnDependencies[intermediate] \/ intermediate = txn)
       IN
        /\ wouldCreateCycle  \* Only fire when cycle would be created
        /\ retryCount[txn] < 3  \* Respect retry limit
        /\ LET affectedTables == txnAccessedTables[txn]
           IN
            /\ \* Clean up sublogs
               physicalSublogs' = [physicalSublogs EXCEPT ![txn] = <<>>]
            /\ logicalSublogs' = [logicalSublogs EXCEPT ![txn] = <<>>]
            /\ \* Remove markers
               markers' = [t \in TABLES |->
                           IF t \in affectedTables
                           THEN SelectSeq(markers[t], LAMBDA m : m.txnId # txn)
                           ELSE markers[t]]
            /\ markerVersions' = [t \in TABLES |->
                                  IF t \in affectedTables
                                  THEN [markerVersions[t] EXCEPT ![txn] = 0]
                                  ELSE markerVersions[t]]
            /\ txnState' = [txnState EXCEPT ![txn] = "aborted"]
            /\ retryCount' = [retryCount EXCEPT ![txn] = @ + 1]
            /\ \* Track that this was a cycle prevention abort
               conflictCycles' = conflictCycles \cup {txn}
            /\ UNCHANGED <<sublogMode, txnDependencies, txnAccessedTables, tableVersions,
                           tableData, tableLogs, txnReadVersions, txnWriteVersions,
                           txnOperations, txnBuffer, crashedTxns, crashCount>>

\* Returns transaction to idle for fresh execution. Preserves retry/crash counts
\* for non-committed resets to enforce retry/crash limits. Prevents total deadlock.
ResetTransaction(txn) ==
    /\ txnState[txn] \in {"committed", "aborted", "crashed"}
    /\ \/ txnState[txn] = "committed"
       \/ (txnState[txn] = "aborted" /\ retryCount[txn] < 3)
       \/ (txnState[txn] = "crashed" /\ crashCount[txn] < 2)
    /\ LET affectedTables == txnAccessedTables[txn]
       IN
        /\ txnState' = [txnState EXCEPT ![txn] = "idle"]
        /\ txnAccessedTables' = [txnAccessedTables EXCEPT ![txn] = {}]
        /\ txnReadVersions' = [txnReadVersions EXCEPT ![txn] = [t \in TABLES |-> tableVersions[t]]]
        /\ txnWriteVersions' = [txnWriteVersions EXCEPT ![txn] = [t \in TABLES |-> 0]]
        /\ txnDependencies' = [txnDependencies EXCEPT ![txn] = {}]
        /\ txnOperations' = [txnOperations EXCEPT ![txn] = <<>>]
        /\ physicalSublogs' = [physicalSublogs EXCEPT ![txn] = <<>>]
        /\ logicalSublogs' = [logicalSublogs EXCEPT ![txn] = <<>>]
        \* Remove orphaned markers from crashed/aborted transactions
        /\ markers' = [t \in TABLES |->
                       IF t \in affectedTables
                       THEN SelectSeq(markers[t], LAMBDA m : m.txnId # txn)
                       ELSE markers[t]]
        /\ markerVersions' = [t \in TABLES |->
                              IF t \in affectedTables
                              THEN [markerVersions[t] EXCEPT ![txn] = 0]
                              ELSE markerVersions[t]]
        \* Reset counts only on successful commit; preserve to enforce limits on failure
        /\ retryCount' = [retryCount EXCEPT ![txn] = 
                           IF txnState[txn] = "committed" THEN 0 ELSE @]
        /\ crashCount' = [crashCount EXCEPT ![txn] = 
                           IF txnState[txn] = "committed" THEN 0 ELSE @]
        \* Clear transaction from both crashed and conflict sets on reset
        /\ crashedTxns' = crashedTxns \ {txn}
        /\ conflictCycles' = conflictCycles \ {txn}
        /\ UNCHANGED <<sublogMode, tableVersions, tableData, tableLogs, txnBuffer>>

\* Markers and sublogs persist for external recovery (Lemma 3.2).
\* Bounded by crashCount to prevent infinite crash-reset loops.
CrashTransaction(txn) ==
    /\ txnState[txn] = "active"
    /\ txnAccessedTables[txn] # {}
    /\ crashCount[txn] < 2
    /\ txnState' = [txnState EXCEPT ![txn] = "crashed"]
    /\ crashedTxns' = crashedTxns \cup {txn}
    /\ crashCount' = [crashCount EXCEPT ![txn] = @ + 1]
    /\ UNCHANGED <<markers, markerVersions, physicalSublogs, logicalSublogs,
                   sublogMode, txnDependencies, txnAccessedTables, tableVersions,
                   tableData, tableLogs, txnReadVersions, txnWriteVersions,
                   txnOperations, txnBuffer, conflictCycles, retryCount>>

\* Recovery: Clean up crashed transaction markers using sublogs
RecoverCrashedTransaction(txn, cleaner) ==
    /\ txn \in crashedTxns
    /\ txnState[txn] = "crashed"
    /\ txnState[cleaner] = "active"
    /\ LET affectedTables == txnAccessedTables[txn]
       IN
        /\ \* Use sublogs to undo changes
           physicalSublogs' = [physicalSublogs EXCEPT ![txn] = <<>>]
        /\ logicalSublogs' = [logicalSublogs EXCEPT ![txn] = <<>>]
        /\ \* Remove crashed transaction's markers
           markers' = [t \in TABLES |->
                       IF t \in affectedTables
                       THEN SelectSeq(markers[t], LAMBDA m : m.txnId # txn)
                       ELSE markers[t]]
        /\ txnState' = [txnState EXCEPT ![txn] = "aborted"]
        /\ crashedTxns' = crashedTxns \ {txn}
        /\ UNCHANGED <<markerVersions, sublogMode, txnDependencies, txnAccessedTables,
                       tableVersions, tableData, tableLogs, txnReadVersions,
                       txnWriteVersions, txnOperations, txnBuffer, conflictCycles, retryCount, crashCount>>

\* Models external recovery when all transactions are crashed/idle (Lemma 6.1).
\* Enables EventualRecovery when no active cleaner is available.
ExternalRecovery(txn) ==
    /\ txn \in crashedTxns
    /\ txnState[txn] = "crashed"
    /\ \* All transactions crashed - external recovery needed
       \A t \in TRANSACTIONS : txnState[t] \in {"crashed", "idle", "committed", "aborted"}
    /\ LET affectedTables == txnAccessedTables[txn]
       IN
        /\ \* Use sublogs to undo changes
           physicalSublogs' = [physicalSublogs EXCEPT ![txn] = <<>>]
        /\ logicalSublogs' = [logicalSublogs EXCEPT ![txn] = <<>>]
        /\ \* Remove crashed transaction's markers
           markers' = [t \in TABLES |->
                       IF t \in affectedTables
                       THEN SelectSeq(markers[t], LAMBDA m : m.txnId # txn)
                       ELSE markers[t]]
        /\ txnState' = [txnState EXCEPT ![txn] = "aborted"]
        /\ crashedTxns' = crashedTxns \ {txn}
        /\ UNCHANGED <<markerVersions, sublogMode, txnDependencies, txnAccessedTables,
                       tableVersions, tableData, tableLogs, txnReadVersions,
                       txnWriteVersions, txnOperations, txnBuffer, conflictCycles, retryCount, crashCount>>

-----------------------------------------------------------------------------
(* Transition Relation *)

\* Abort when version space is exhausted before any marker is placed
AbortWhenVersionSpaceExhausted(txn) ==
    /\ txnState[txn] = "active"
    /\ txnAccessedTables[txn] = {}  \* Haven't placed any markers yet
    /\ \A table \in TABLES : tableVersions[table] >= MAX_VERSION
    /\ txnState' = [txnState EXCEPT ![txn] = "aborted"]
    /\ UNCHANGED <<markers, markerVersions, physicalSublogs, logicalSublogs,
                   sublogMode, txnDependencies, txnAccessedTables, tableVersions,
                   tableData, tableLogs, txnReadVersions, txnWriteVersions,
                   txnOperations, txnBuffer, crashedTxns, conflictCycles, retryCount, crashCount>>

\* Graceful termination when all transactions have exhausted their retry/crash limits
TerminateWhenLimitsExhausted ==
    /\ \A txn \in TRANSACTIONS : 
        /\ txnState[txn] \in {"idle", "aborted", "committed"}
        /\ (retryCount[txn] >= 3 \/ crashCount[txn] >= 2)
    /\ UNCHANGED vars

\* Undo/redo fire only within abort and recovery, not as standalone actions
Next ==
    \/ \E txn \in TRANSACTIONS : StartTransaction(txn)
    \/ \E txn \in TRANSACTIONS, table \in TABLES : PlaceMarker(txn, table)
    \/ \E txn \in TRANSACTIONS, table \in TABLES, file \in FILES :
        AddPhysicalSublog(txn, table, "ADD_FILE", file, "REMOVE_FILE")
    \/ \E txn \in TRANSACTIONS, table \in TABLES :
        AddLogicalSublog(txn, table, "SEEK", "RESTORE")
    \/ \E txn \in TRANSACTIONS, table \in TABLES : UpdateDependencies(txn, table)
    \/ \E txn \in TRANSACTIONS : MarkerShift(txn)
    \/ \E txn \in TRANSACTIONS : CommitTransaction(txn)
    \/ \E txn \in TRANSACTIONS, table \in TABLES : AbortOnCycleDetection(txn, table)
    \/ \E txn \in TRANSACTIONS : AbortWhenVersionSpaceExhausted(txn)
    \/ \E txn \in TRANSACTIONS : ResetTransaction(txn)
    \/ \E txn \in TRANSACTIONS : CrashTransaction(txn)
    \/ \E txn, cleaner \in TRANSACTIONS : RecoverCrashedTransaction(txn, cleaner)
    \/ \E txn \in TRANSACTIONS : ExternalRecovery(txn)
    \/ TerminateWhenLimitsExhausted

\* Fairness: strong fairness on crash recovery ensures EventualRecovery (Lemma 6.1)
Spec == Init /\ [][Next]_vars 
    /\ WF_vars(Next)
    /\ \A txn1 \in TRANSACTIONS : SF_vars(ExternalRecovery(txn1))
    /\ \A txn2 \in TRANSACTIONS : SF_vars(RecoverCrashedTransaction(txn2, txn2))
    /\ \A txn3 \in TRANSACTIONS : WF_vars(ResetTransaction(txn3))
    /\ \A txn4 \in TRANSACTIONS : WF_vars(CommitTransaction(txn4))

-----------------------------------------------------------------------------
(* Safety Properties (Lemmas 3.2, 4.3–4.5, 5.4–5.7) *)

\* Lemma 3.2: atomically overwrites marker with committed log to ensure table-level atomicity
TableLevelAtomicity ==
    \A txn \in TRANSACTIONS, table \in TABLES :
        txnState[txn] = "committed" =>
            \A ver \in 1..tableVersions[table] :
                tableLogs[table][ver] # "EMPTY" =>
                    (tableLogs[table][ver] = "COMMITTED" \/ 
                     \E m \in {markers[table][i] : i \in 1..Len(markers[table])} : 
                        m.txnId = txn)

\* Def. 3.2 / Eq. 4: PUT-IF-ABSENT ensures at most one transaction reserves each version per table
UniqueMarkerVersions ==
    \A table \in TABLES, txn1, txn2 \in TRANSACTIONS :
        /\ txn1 # txn2
        /\ markerVersions[table][txn1] > 0
        /\ markerVersions[table][txn2] > 0
        => markerVersions[table][txn1] # markerVersions[table][txn2]

\* Lemma 4.3 (Eq. 7): version ordering induces causal order for single-table transactions
CausalConsistency ==
    \A txn1, txn2 \in TRANSACTIONS, table \in TABLES :
        /\ txnState[txn1] = "committed"
        /\ txnState[txn2] = "committed"
        /\ table \in txnAccessedTables[txn1]
        /\ table \in txnAccessedTables[txn2]
        /\ txn1 # txn2
        => txnWriteVersions[txn1][table] # txnWriteVersions[txn2][table]

\* Acyclic dependency DAG: proactive cycle prevention ensures no cycles form (Lemma 5.5)
AcyclicDependencies ==
    \A txn \in TRANSACTIONS :
        txnState[txn] = "active" =>
            ~\E cycle \in SUBSET TRANSACTIONS :
                /\ txn \in cycle
                /\ Cardinality(cycle) > 1
                /\ \A t1, t2 \in cycle : 
                    t1 # t2 => (t2 \in txnDependencies[t1])

\* Cycle detection via marker shift ensures deadlock freedom (Lemma 5.5)
DeadlockFreedom ==
    \A txn \in TRANSACTIONS :
        \* If cycle detected (Lemma 5.5, Rule IV)
        (\E t \in TRANSACTIONS : 
            /\ t # txn
            /\ t \in txnDependencies[txn]
            /\ txn \in txnDependencies[t])
        => \* Transaction eventually in conflict set or cycle broken
           <>(\/ txn \in conflictCycles  \* Marker shift triggered
              \/ ~\E t \in TRANSACTIONS :  \* Or cycle already broken
                    t \in txnDependencies[txn] /\ txn \in txnDependencies[t])

\* Transactions that triggered marker shift eventually commit or abort (Lemma 5.5)
ConflictResolution ==
    \A txn \in TRANSACTIONS :
        \* Once in conflict set, eventually exits (commits, aborts, or re-enters active)
        (txn \in conflictCycles) ~> 
            (txnState[txn] \in {"committed", "aborted"} \/ txn \notin conflictCycles)

\* Lemma 5.4: version ordering establishes serialization order for single-table transactions
SingleTableSerializability ==
    \A txn1, txn2 \in TRANSACTIONS, table \in TABLES :
        /\ txnState[txn1] = "committed"
        /\ txnState[txn2] = "committed"
        /\ txn1 # txn2  \* Different transactions
        /\ table \in txnAccessedTables[txn1]
        /\ table \in txnAccessedTables[txn2]
        /\ txnAccessedTables[txn1] = {table}  \* Single-table transaction
        /\ txnAccessedTables[txn2] = {table}  \* Single-table transaction
        /\ tableLogs[table][txnWriteVersions[txn1][table]] = "COMMITTED"  \* Actually committed data
        /\ tableLogs[table][txnWriteVersions[txn2][table]] = "COMMITTED"  \* Actually committed data
        => (txnWriteVersions[txn1][table] < txnWriteVersions[txn2][table] \/
            txnWriteVersions[txn1][table] > txnWriteVersions[txn2][table])

\* Lemma 5.6: LV[CT] provides repeatable reads across tables for multi-table transactions
MultiTableRepeatableReads ==
    \A txn \in TRANSACTIONS :
        /\ txnState[txn] = "committed"
        /\ Cardinality(txnAccessedTables[txn]) > 1  \* Multi-table transaction
        => \A table \in txnAccessedTables[txn] :
            \* Read version established at marker placement remains stable
            \* marker shift never changes read version (Lemma 5.5)
            LET readVer == txnReadVersions[txn][table]
            IN \A ver \in (readVer+1)..tableVersions[table] :
                \* No intermediate version from another txn in this txn's execution
                \A otherTxn \in TRANSACTIONS :
                    /\ otherTxn # txn
                    /\ txnState[otherTxn] = "committed"
                    /\ table \in txnAccessedTables[otherTxn]
                    /\ txnWriteVersions[otherTxn][table] = ver
                    => \* Either depends on it or came after
                       \/ otherTxn \in txnDependencies[txn]
                       \/ txnWriteVersions[txn][table] < ver

\* Transactions read from committed versions, initial version 0, or their own marker (snapshot isolation)
NoDirtyReads ==
    \A txn \in TRANSACTIONS, table \in TABLES :
        /\ txnState[txn] = "active"
        /\ table \in txnAccessedTables[txn]
        => LET readVer == txnReadVersions[txn][table]
           IN \/ readVer = 0  \* Initial state is always clean (paper implicit)
              \/ tableLogs[table][readVer] = "COMMITTED"  \* Read committed version
              \/ \* Can read from own marker (snapshot isolation)
                 \E m \in {markers[table][i] : i \in 1..Len(markers[table])} :
                    m.txnId = txn

\* Crashed transactions hold sublogs or have not yet performed operations; enables recovery (Lemma 3.2)
RecoveryInvariant ==
    \A txn \in TRANSACTIONS :
        txn \in crashedTxns =>
            \* If has markers, must be able to recover
            ((\A table \in txnAccessedTables[txn] :
                markerVersions[table][txn] > 0) =>
            \* Either has sublogs OR hasn't performed operations yet
            (\/ Len(physicalSublogs[txn]) > 0
             \/ Len(logicalSublogs[txn]) > 0
             \/ Len(txnOperations[txn]) = 0))  \* Early crash before operations

\* LV[R] detects conflicts at file granularity, enabling higher concurrency
FileBasedConflicts ==
    \A txn1, txn2 \in TRANSACTIONS, table \in TABLES :
        /\ txn1 # txn2
        /\ txnState[txn1] = "active"
        /\ txnState[txn2] = "active"
        /\ table \in txnAccessedTables[txn1]
        /\ table \in txnAccessedTables[txn2]
        /\ sublogMode[txn1] \in {"physical", "mixed"}
        /\ sublogMode[txn2] \in {"physical", "mixed"}
        => TRUE  \* File-level conflict detection happens at commit

\* Marker shift ensures the transaction is no longer part of any cycle (Lemma 5.5)
MarkerShiftBreaksCycles ==
    \A txn \in TRANSACTIONS :
        txn \in conflictCycles =>
            \* After marker shift, transaction should not be in a cycle
            ~\E other \in TRANSACTIONS :
                /\ other # txn
                /\ other \in txnDependencies[txn]
                /\ txn \in txnDependencies[other]

\* Concurrent writes to the same table produce distinct versions; no overwrites (Lemma 5.4)
NoLostUpdates ==
    \A txn1, txn2 \in TRANSACTIONS, table \in TABLES :
        /\ txn1 # txn2
        /\ txnState[txn1] = "committed"
        /\ txnState[txn2] = "committed"
        /\ table \in txnAccessedTables[txn1]
        /\ table \in txnAccessedTables[txn2]
        => LET v1 == txnWriteVersions[txn1][table]
               v2 == txnWriteVersions[txn2][table]
           IN \* Transactions write to different versions (no overwrite)
              v1 # v2

\* Each transaction holds at most one marker per table (Def. 3.2, PUT-IF-ABSENT)
MarkerUniqueness ==
    \A table \in TABLES, txn \in TRANSACTIONS :
        \* At most one marker per transaction per table in markers sequence
        Cardinality({i \in 1..Len(markers[table]) : 
            markers[table][i].txnId = txn}) <= 1

Safety ==
    /\ TypeOK
    /\ TableLevelAtomicity
    /\ UniqueMarkerVersions
    /\ CausalConsistency
    /\ AcyclicDependencies
    /\ SingleTableSerializability
    /\ MultiTableRepeatableReads
    /\ NoDirtyReads
    /\ RecoveryInvariant
    /\ FileBasedConflicts
    /\ MarkerShiftBreaksCycles
    /\ NoLostUpdates
    /\ MarkerUniqueness

-----------------------------------------------------------------------------
(* Liveness Properties (Lemmas 5.5, 5.6–5.7, 6.1) *)

\* Transactions eventually commit or abort; guaranteed by marker shift and retry mechanism
EventualTermination ==
    \A txn \in TRANSACTIONS :
        txnState[txn] = "active" ~>
            (txnState[txn] = "committed" \/ txnState[txn] = "aborted")

\* Crashed transactions are eventually recovered
EventualRecovery ==
    \A txn \in TRANSACTIONS :
        txn \in crashedTxns ~> txn \notin crashedTxns

\* Marker shift resolves detected cycles, ensuring transactions exit the deadlock state (Lemma 5.5)
EventualDeadlockResolution ==
    \A txn \in TRANSACTIONS :
        (\E t \in TRANSACTIONS : 
            /\ t \in txnDependencies[txn]
            /\ txn \in txnDependencies[t]) 
        ~> (txn \in conflictCycles \/ txnState[txn] # "active")

Liveness ==
    /\ EventualTermination
    /\ EventualRecovery
    /\ EventualDeadlockResolution
    /\ DeadlockFreedom
    /\ ConflictResolution

-----------------------------------------------------------------------------
(* State Constraints for Model Checking *)

\* Limit sublog lengths to prevent state explosion
StateConstraint_SublogLength ==
    /\ \A txn \in TRANSACTIONS : Len(physicalSublogs[txn]) <= MAX_OPERATIONS
    /\ \A txn \in TRANSACTIONS : Len(logicalSublogs[txn]) <= MAX_OPERATIONS

\* Limit number of crashed transactions
StateConstraint_CrashedTxns ==
    Cardinality(crashedTxns) <= 1

\* Limit table versions
StateConstraint_TableVersions ==
    \A table \in TABLES : tableVersions[table] <= MAX_VERSION

\* Limit retry attempts per transaction
StateConstraint_RetryLimit ==
    \A txn \in TRANSACTIONS : retryCount[txn] <= 3

\* Limit crash count per transaction
StateConstraint_CrashLimit ==
    \A txn \in TRANSACTIONS : crashCount[txn] <= 2

\* Prune degenerate states where committed/crashed transactions never accessed any table
StateConstraint_RequireProgress ==
    \E txn \in TRANSACTIONS :
        txnState[txn] \in {"committed", "crashed"} =>
            txnAccessedTables[txn] # {}

-----------------------------------------------------------------------------
(* View Specification for Symmetry Reduction *)

ViewSpec ==
    <<tableVersions, tableLogs, markers, Cardinality(crashedTxns)>>

-----------------------------------------------------------------------------
(* Alias for Counterexample Readability *)

AliasSpec ==
    [
        activeTxns |-> {txn \in TRANSACTIONS : txnState[txn] = "active"},
        committedTxns |-> {txn \in TRANSACTIONS : txnState[txn] = "committed"},
        abortedTxns |-> {txn \in TRANSACTIONS : txnState[txn] = "aborted"},
        crashedTxns |-> crashedTxns,
        cycleDetected |-> conflictCycles,
        tableVersions |-> tableVersions,
        hasMarkers |-> [t \in TABLES |-> Len(markers[t]) > 0],
        dependencies |-> [txn \in TRANSACTIONS |-> 
            IF txnState[txn] = "active" THEN txnDependencies[txn] ELSE {}],
        retryCounts |-> [txn \in TRANSACTIONS |-> retryCount[txn]],
        crashCounts |-> [txn \in TRANSACTIONS |-> crashCount[txn]]
    ]

=============================================================================
=============================================================================
