#!/usr/bin/env bash
# triage.sh LOG... — one TSV row per distinct race site across the given ThreadSanitizer logs:
#
#   hits  file  function-ERE  tiers  state
#
# `file` is repo-relative with any `dir/../` collapsed; `function-ERE` is the SUMMARY site's function, escaped and
# anchored so it can be pasted into races.txt as is; `tiers` names the racing threads' tiers (deluge-audio → audio,
# deluge-stream → storage, deluge-bsp-host → interaction, the main thread → main); `state` is what TSan says the
# racing address is ("global 'NAME'", "heap", or "-"). Rows are sorted by hits, most first.
#
# The raw material for races.txt entries; it decides nothing.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"

awk -v root="$REPO_ROOT/" '
function tier(id,   n) {
    if (id == "main") return "main"
    n = names[id]
    if (n == "deluge-audio") return "audio"
    if (n == "deluge-stream") return "storage"
    if (n == "deluge-bsp-host") return "interaction"
    return (n == "" ? id : n)
}
function ere(s) {
    gsub(/[][\\.*+?(){}|^$]/, "\\\\&", s)
    return "^" s "$"
}
/^WARNING: ThreadSanitizer: data race/ { inblock = 1; nacc = 0; state = "-"; delete names; next }
!inblock { next }
match($0, /^  (Previous )?[A-Za-z ]+ of size [0-9]+ at [^ ]+ by (thread (T[0-9]+)|main thread)/, m) {
    acc[++nacc] = (m[3] == "" ? "main" : m[3]); next
}
match($0, /Location is global .([^ ]+). /, m) { state = "global \x27" m[1] "\x27"; next }
/Location is heap block/ { state = "heap"; next }
match($0, /^  Thread (T[0-9]+) .([^\x27]*)\x27/, m) { names[m[1]] = m[2]; next }
/^SUMMARY: ThreadSanitizer: data race/ {
    inblock = 0
    site = $0; sub(/^SUMMARY: ThreadSanitizer: data race /, "", site); gsub(root, "", site)
    file = "-"; fn = site
    if (match(site, /^([^ :]+):[0-9]+(:[0-9]+)? in (.+)$/, m)) {
        file = m[1]; fn = m[3]
        while (sub(/[^\/]+\/\.\.\//, "", file)) {}
    } else if (match(site, / in (.+)$/, m)) {
        fn = m[1]
    }
    a = (nacc >= 1 ? tier(acc[1]) : "?"); b = (nacc >= 2 ? tier(acc[2]) : "?")
    t = (a < b ? a "×" b : b "×" a)
    key = file "\t" ere(fn)
    hits[key]++
    if (index("," tiers[key] ",", "," t ",") == 0) tiers[key] = (tiers[key] == "" ? t : tiers[key] "," t)
    if (states[key] == "" || states[key] == "-") states[key] = state
}
END { for (k in hits) printf "%d\t%s\t%s\t%s\n", hits[k], k, tiers[k], states[k] }
' "$@" | sort -t$'\t' -k1,1nr -k2,2 -k3,3
