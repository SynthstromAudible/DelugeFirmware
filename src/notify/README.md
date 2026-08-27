# deluge_notify

The model→UI notification channel: `DirtySet` (facts, pointer-free) and `NoticeQueue`
(presentation requests, copied payloads). Spec: docs/superpowers/specs/2026-08-26-ui-notifications-design.md.

Rules: include nothing but `notify/**` and the standard library; `raise*`/`push` are [any]
(atomic, allocation-free, no UI symbols); `drain`/`pop` are [task]. Typed raisers that know
`Error` and `l10n::String` live in `src/deluge/util/notify.h`, not here.
