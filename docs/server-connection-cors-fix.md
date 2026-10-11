# Server connection from the bundled chooser

The rc.1 chooser could report a connection failure after successfully reading
`/System/Info/Public`. It also fetched the server's root HTML from its local
`file://` page. Reverse proxies can permit CORS for the API while omitting CORS
headers on static HTML or the root redirect; the second fetch then failed even
though opening the same server in an ordinary browser worked.

The installed rc.1 log confirmed repeated missing-Access-Control-Allow-Origin
failures for the root document, with no public-info, certificate or timeout
failure. The server's public API still returned its Jellyfin ID and version.
The application's broad Qt5 web-security disable flag had been removed during
the Qt6 migration; the redundant root fetch remained behind.

The rc.2 chooser performs the public API check, waits for the NativeShell
settings interface, persists the canonical server URL with an acknowledged
WebChannel call and then navigates to the server. It neither fetches nor rewrites
the HTML. The persistent profile injects NativeShell and its plugins on the real
HTTP(S) document, including with a strict CSP. NativeShell refreshes its settings
from the native interface before exposing the new document's settings proxies,
so a first file-to-HTTP transition sees the saved server even though the profile
script's startup snapshot and the origins' sessionStorage differ. Certificate
validation and CORS remain enabled.

Addresses are trimmed and normalized with the URL parser. HTTP(S) and base-path
installations are supported; credentials, queries, fragments and other schemes
are rejected. Failed or malformed public-info responses restore the connect
button, allowing a retry.

`scripts/test-find-webclient.js` checks the actual chooser, request ordering,
acknowledged persistence, malformed/failing responses and retries. The isolated
NativeShell smoke now exercises the chooser against an API permitting CORS and
a strict-CSP root document without CORS headers, then verifies its real document
origin, injected plugins and native settings. These checks use synthetic data;
no server implementation or user account is changed.

The application version is `2.0.0-rc.2`, with MSI/Burn package version `2.0.2`
so the corrected installer can upgrade the earlier `2.0.1` package. Dependency
versions are unchanged and Qt release qualification remains false. This fix
does not supply new memory/performance qualification or user acceptance.
