# Pinned Web loses clearQueue on SyncPlay wrapper unbind

At served Web commit `b2a97d40c6898726b030b0233a0c9dc89734d3e3`,
`src/plugins/syncPlay/ui/players/NoActivePlayer.js` saves the original function
on `playbackManager._localClearQueue` at line 62, but unbind restores
`playbackManager.clearQueue` from `this._localClearQueue` at line 114.
The inherited GenericPlayer constructor never defines that instance member.
Consequently the actual wrapper bind/unbind sequence replaces the manager's
clearQueue function with `undefined`; calling it afterward throws `TypeError`.
HtmlVideoPlayer inherits NoActivePlayer, so the affected lifecycle is also
shared by that wrapper.

This was reproduced using the complete pinned GenericPlayer and NoActivePlayer
classes with only import dependencies mocked. Both fixture SHA256 values were
verified before execution. No Desktop adapter participates in the reproduction,
and no real browser, application, account, group or server is contacted.

| Pinned source | SHA256 |
| --- | --- |
| GenericPlayer.js | `68c11b08e52c662bdc34a116dd6d6fdd49ffbdf55d62236939d1c52f1bfa7d19` |
| NoActivePlayer.js | `601fdd25c1002b8b4701ab8f8ae2c0b7e8e0c50792fa84964085589cab86a112` |

From this Desktop checkout, run the retained read-only evidence script:

```powershell
$nodeExecutable = (Get-Command node.exe -ErrorAction Stop).Source
& $nodeExecutable ./quality/observations/pinned-web-clearqueue-repro.cjs
if ($LASTEXITCODE -ne 0) { throw 'Pinned source reproduction failed' }
```

The script is a local source reproduction under quality, outside normal build/release validation. It imports no production
module beyond the two immutable test fixtures and preserves their complete
class method bodies. It asserts the original function exists, binds the group
override, verifies that the original was saved on playbackManager, invokes the
real inherited unbind method, and checks both `undefined` and the resulting
`TypeError`. Expected output:

```json
{"commit":"b2a97d40c6898726b030b0233a0c9dc89734d3e3","beforeBind":"function","duringBind":"group override; original saved on playbackManager","afterUnbind":"undefined","callingAfterLeave":"TypeError","reproduced":true}
```

The Server/Web agent owns any correction and regression coverage for restoring
the saved callback during leave/rejoin/player changes. A subsequent Web release
would require a new reviewed handoff/pin. Desktop should continue consuming the
agreed contract; hiding this defect with a client-specific callback replacement
would not repair the served Web lifecycle. The isolated source reproduction
confirms the callback loss, while final real group acceptance remains separate.

The reproduction script SHA256 is `5575b5fa639b5454a123071e837ae510af97491e11b623134abdf0145778cea7`.
The two pinned fixtures retain their upstream license in
`tests/syncplay/web-reference/LICENSE` and exact identities in
`tests/syncplay/web-reference/source-pins.json`. The current Server pin
`494362e6b354385a00b7ae879814e2908c23f725` and Web pin remain unchanged.
This note is evidence for the Server/Web owner, not a sent message or a server
deployment. It does not establish real mixed-client leave/rejoin acceptance.
