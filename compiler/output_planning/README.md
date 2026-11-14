# Output Planning

The output-planning layer builds the language-neutral generation plan after
source-unit loading and semantic analysis.

Output is planned per **namespace**, not per source file. `OutputPlan::namespaces`
lists every namespace of the compilation root's import closure that declares
records, sorted by FQN, with the source units contributing to it. All records of
a namespace are generated together, so several files declaring records in one
namespace share one set of generated artifacts instead of colliding.

`GenerationSelection` (`--generate`) decides which namespaces are emitted:

* an explicit selection emits exactly the listed namespaces; a namespace no
  record declares is rejected with `BC8002`;
* without a selection, a record root emits its own namespace (the historical
  behavior), and a configuration root emits every namespace.

Unselected namespaces remain planned, so emitted files can include or import
their bindings by planned path. Those bindings must be generated from the same
root; the planner never emits them implicitly. Selection does not affect Schema
IR, layout, record IDs or QBS, which always cover the complete closure.

`OutputPlan::units` lists the record-declaring source units in the
deterministic dependency-first order established by `CompilerContext`, each
with its namespace's `emits_output` value and its deduplicated imported-unit
dependencies. A configuration unit declares nothing and is not listed;
`OutputPlan::root_is_configuration` records that the root is one.

Backend-specific extensions, include paths, Python package files, and concrete
artifact rendering remain backend responsibilities. Backends derive both
`--list-outputs` and generation from the same plan.
