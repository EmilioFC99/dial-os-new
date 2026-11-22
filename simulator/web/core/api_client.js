/**
 * Graceful HTTP/JSON client helpers for the DialOS simulator Python backend.
 * Never throws uncaught errors on network or parse failures.
 */

/**
 * Execute a fetch request and normalize success/failure into `{ ok, payload | error }`.
 *
 * @param {string} routePath Relative API endpoint path.
 * @param {RequestInit} requestOptions Fetch options.
 * @returns {Promise<{ ok: true, payload: unknown } | { ok: false, error: string }>} Normalized response envelope.
 */
async function requestJson(routePath, requestOptions = {}) {
  try {
    const httpResponse = await fetch(routePath, requestOptions);
    let payload = null;
    try {
      payload = await httpResponse.json();
    } catch (parseError) {
      if (!httpResponse.ok) {
        return {
          ok: false,
          error: `HTTP ${httpResponse.status} (${httpResponse.statusText || "request_failed"})`,
        };
      }
      const message = parseError instanceof Error ? parseError.message : "invalid_json";
      return { ok: false, error: message };
    }

    if (!httpResponse.ok) {
      const serverError =
        payload && typeof payload.error === "string"
          ? payload.error
          : `HTTP ${httpResponse.status}`;
      return { ok: false, error: serverError };
    }

    return { ok: true, payload };
  } catch (networkError) {
    const errorMessage =
      networkError instanceof Error ? networkError.message : String(networkError);
    return { ok: false, error: errorMessage || "network_error" };
  }
}

/**
 * Trigger an asynchronous backend boot library scan.
 *
 * @param {object} [options] Scan options.
 * @param {boolean} [options.rescan=true] Whether to force a fresh scan.
 * @returns {Promise<{ ok: true, payload: object } | { ok: false, error: string }>} Boot snapshot response.
 */
export function triggerBootScan({ rescan = true } = {}) {
  return requestJson("/api/boot", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ rescan: Boolean(rescan) }),
    cache: "no-store",
  });
}

/**
 * Poll current backend boot and scan progress without caching.
 *
 * @returns {Promise<{ ok: true, payload: object } | { ok: false, error: string }>} Latest boot status.
 */
export function fetchBootStatus() {
  return requestJson("/api/boot/status", {
    method: "GET",
    cache: "no-store",
  });
}

/**
 * Fetch the full serialized library catalog without caching.
 *
 * @returns {Promise<{ ok: true, payload: object } | { ok: false, error: string }>} Library catalog payload.
 */
export function fetchLibraryCatalog() {
  return requestJson("/api/library", {
    method: "GET",
    cache: "no-store",
  });
}

/**
 * Trigger a full library rescan and return the refreshed catalog.
 *
 * @returns {Promise<{ ok: true, payload: object } | { ok: false, error: string }>} Refreshed catalog payload.
 */
export function triggerLibraryRescan() {
  return requestJson("/api/rescan", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({}),
    cache: "no-store",
  });
}
