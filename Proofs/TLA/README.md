# LakeVilla Verification

Formal verification of the LakeVilla protocol variants using TLA+ and the TLC model checker:

- **LV[Write] = LV[R] + LV[CT]** — `LakeVillaWriteOptimized_spec.tla`
- **LV[Read] = LV[I]** — `LakeVillaReadOptimized_spec.tla`

## Prerequisites

- Java (tested with OpenJDK 64-Bit SapMachine `17.0.17+10-LTS`)
- `tla2tools.jar` in the current directory

> **Obtaining `tla2tools.jar`:** Download the latest release from the [TLA+ GitHub releases page](https://github.com/tlaplus/tlaplus/releases) (file `tla2tools.jar`). The tool is distributed under the [MIT License](https://github.com/tlaplus/tlaplus/blob/master/LICENSE).

## Running TLC

```powershell
java -Xmx4g -cp tla2tools.jar tlc2.TLC -workers auto -config <CONFIG> LakeVillaWriteOptimized_spec.tla
```

---

## Write-Optimized Protocol (`LakeVillaWriteOptimized_spec.tla`)

### `LakeVillaWriteOptimized_Quick.cfg`
Fast sanity check (1 table, 2 transactions). Verifies core serializability and atomicity invariants — no liveness, no cycle checking. Run time: < 2 min.

```powershell
java -Xmx4g -cp tla2tools.jar tlc2.TLC -workers auto -config LakeVillaWriteOptimized_Quick.cfg LakeVillaWriteOptimized_spec.tla
```

Checks: `TypeOK`, `SingleTableSerializability`, `NoLostUpdates`, `NoDirtyReads`, `UniqueMarkerVersions`, `MarkerUniqueness`

---

### `LakeVillaWriteOptimized.cfg`
Standard check (2 tables, 3 transactions). Full set of safety invariants plus liveness properties.

```powershell
java -Xmx4g -cp tla2tools.jar tlc2.TLC -workers auto -config LakeVillaWriteOptimized.cfg LakeVillaWriteOptimized_spec.tla
```

Checks: `Safety` (all safety invariants including `TableLevelAtomicity`, `CausalConsistency`, `AcyclicDependencies`, `MarkerShiftBreaksCycles`, `RecoveryInvariant`), plus `EventualTermination`, `EventualDeadlockResolution`, `EventualRecovery`

---

### `LakeVillaWriteOptimized_Deep.cfg`
Thorough check with larger state space (2 tables, 3 transactions, 5 versions, 4 operations). Same properties as standard, but broader exploration.

```powershell
java -Xmx8g -cp tla2tools.jar tlc2.TLC -workers auto -config LakeVillaWriteOptimized_Deep.cfg LakeVillaWriteOptimized_spec.tla
```

Checks: `Safety`, `Liveness` (all safety + all liveness, including `DeadlockFreedom` and `ConflictResolution`)

---

### `LakeVillaWriteOptimized_NoCycles.cfg`
Strict no-cycles variant (2 tables, 2 transactions). Enforces `AcyclicDependencies` as a hard invariant — cycles are never permitted, not even transiently. Validates that proactive cycle prevention in `UpdateDependencies` holds at all times (Lemma 5.5).

```powershell
java -Xmx4g -cp tla2tools.jar tlc2.TLC -workers auto -config LakeVillaWriteOptimized_NoCycles.cfg LakeVillaWriteOptimized_spec.tla
```

Checks: `AcyclicDependencies`, `MarkerShiftBreaksCycles`, `SingleTableSerializability`, `NoLostUpdates`, `NoDirtyReads`, `UniqueMarkerVersions`, `MarkerUniqueness`

---

### `LakeVillaWriteOptimized_Deadlock.cfg`
Deadlock-focused check (2 tables, 2 transactions). Tests that marker shift correctly breaks dependency cycles and that serializability is preserved during conflict resolution (Lemma 5.5).

```powershell
java -Xmx4g -cp tla2tools.jar tlc2.TLC -workers auto -config LakeVillaWriteOptimized_Deadlock.cfg LakeVillaWriteOptimized_spec.tla
```

Checks: `MarkerShiftBreaksCycles`, `SingleTableSerializability`, `NoLostUpdates`, `UniqueMarkerVersions`, `MarkerUniqueness`

---

### `LakeVillaWriteOptimized_Temporal.cfg`
Temporal / liveness check (2 tables, 2 transactions). Verifies that transactions eventually terminate, crashed transactions are eventually recovered, and deadlocks are eventually resolved.

```powershell
java -Xmx4g -cp tla2tools.jar tlc2.TLC -workers auto -config LakeVillaWriteOptimized_Temporal.cfg LakeVillaWriteOptimized_spec.tla
```

Checks: `TypeOK`, `TableLevelAtomicity`, `CausalConsistency`, `AcyclicDependencies`, `SingleTableSerializability`, `EventualTermination`, `EventualRecovery`, `EventualDeadlockResolution`

---

## Read-Optimized Protocol (`LakeVillaReadOptimized_spec.tla`)

### `LakeVillaReadOptimized_Quick.cfg`
Reduced model (2 tables, 2 transactions, 3 versions) for rapid verification of core safety invariants. Uses explicit `INIT`/`NEXT` without fairness; liveness properties are not checked.

```powershell
java -Xmx4g -cp tla2tools.jar tlc2.TLC -workers auto -config LakeVillaReadOptimized_Quick.cfg LakeVillaReadOptimized_spec.tla
```

Checks: `TypeOK`, `GlobalSerializability`, `R1_NoOutdatedReads`, `R2_UniqueVersions`, `R3_NoLostUpdates`, `MonotonicVersionLog`, `SnapshotIsolation`, `NoDirtyReads`, `AtomicCommit`

---

### `LakeVillaReadOptimized.cfg`
Standard check (3 tables, 3 transactions, 5 versions). Verifies all safety invariants and includes liveness properties. Uses `Spec` (with fairness) to avoid trivial stuttering counterexamples. Liveness properties are omitted for tractability in this configuration.

```powershell
java -Xmx4g -cp tla2tools.jar tlc2.TLC -workers auto -config LakeVillaReadOptimized.cfg LakeVillaReadOptimized_spec.tla
```

Checks: `TypeOK`, `GlobalSerializability`, `R1_NoOutdatedReads`, `R2_UniqueVersions`, `R3_NoLostUpdates`, `MonotonicVersionLog`, `SnapshotIsolation`, `NoDirtyReads`, `AtomicCommit`, `EventualTermination`, `ValidationProgress`


