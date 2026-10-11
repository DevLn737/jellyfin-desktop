async function tryConnect(server) {
    document.getElementById('connect-button').disabled = true;

    try {
        const address = server.trim();
        if (!address) {
            throw new Error("Server address is empty");
        }
        const parsed = new URL(/^[a-z][a-z\d+.-]*:\/\//i.test(address) ? address : "http://" + address);
        if (!['http:', 'https:'].includes(parsed.protocol) || parsed.username || parsed.password ||
            parsed.search || parsed.hash) {
            throw new Error("Invalid server address");
        }
        const serverBaseURL = parsed.href.replace(/\/+$/, "");
        const url = serverBaseURL + "/System/Info/Public";
        const response = await fetch(url, { cache: 'no-cache', signal: AbortSignal.timeout(5000) });
        if (!response.ok) {
            throw new Error("Server public information request failed");
        }
        const info = await response.json();
        if (typeof info?.Id !== 'string' || !info.Id.trim()) {
            throw new Error("Server did not provide a Jellyfin ID");
        }

        // Reading the Web HTML with fetch requires CORS even though opening
        // it as a document does not. NativeShell and its plugins are injected
        // by the persistent profile, including on pages with a strict CSP.
        // Persist before navigation destroys this page's WebChannel context.
        await window.initCompleted;
        await new Promise(resolve => window.api.settings.setValue('main', 'userWebClient', serverBaseURL, resolve));
        window.location.assign(serverBaseURL);
        return true;
    } catch (e) {
        console.error(e);
        document.getElementById('connect-button').disabled = false;
        return false;
    }
}

document.getElementById('connect-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const server = document.getElementById('address').value;
    const result = await tryConnect(server);
    if (!result) {
        document.getElementById('backdrop').style.display = 'block';
    }
});

document.getElementById('connect-fail-button').addEventListener('click', () => {
    document.getElementById('backdrop').style.display = 'none';
});


// load the server if we have one
(async() => {
    const savedServer = window.jmpInfo.settings.main.userWebClient;
    if (savedServer) {
        document.getElementById('address').value = savedServer;
    }

    if (!savedServer || !(await tryConnect(savedServer))) {
        document.getElementById('splash').style.display = 'none';
        document.getElementById('main').style.display = 'block';
    }
})();
