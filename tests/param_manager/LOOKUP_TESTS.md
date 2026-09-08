# Parameter Lookup Tests

`ParamLookupTests` and `ParamLookupTestsNoDiagnostics` execute the production
lookup translation units using the real ModelStack representation, manager
layout predicates, and fixed-array ParamSet lookup implementation.

The tests check AutoParam pointer identity together with its manager, collection,
summary, and ID. Coverage includes MIDI follow (synth, audio and both kit modes),
automation selected-parameter fallback and menu context, patched/unpatched and
patch-cable menus, song lookup, MIDI CC/expression mapping, CV expression,
invalid IDs, incompatible/malformed layouts, failed expression creation, and
reuse of model-stack storage after successful lookups.

Surrounding clip, UI, and collection objects are host doubles. Fixed-size test
arrays deliberately have different sizes. Expression creation failure is injected
by a double; real allocation is covered separately by the manager transfer tests.
Patch-cable tests verify descriptor and creation-flag forwarding, not production
descriptor decoding. Dynamic MIDI storage and full UI event handling are not
executed here. This suite does not establish correctness of every lookup caller
or every production parameter descriptor.

Invalid lookups follow existing API conventions: either the returned stack is
null, or its AutoParam is null. Callers must check both before accessing it.