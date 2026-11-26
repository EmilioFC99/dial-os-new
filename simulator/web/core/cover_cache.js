/**
 * Headless-safe bounded LRU cache for asynchronous album artwork image loading.
 */

const DEFAULT_MAX_CACHE_ENTRIES = 48;

/**
 * Create an LRU cache of loaded HTMLImageElement entries keyed by album identity and URL.
 * Returns null safely from `getOrLoadAlbumCover` when `coverUrl` is missing or in headless Node.
 *
 * @param {object} [options] Cache configuration options.
 * @param {number} [options.maxEntries=48] Maximum retained cover image entries before LRU eviction.
 * @returns {{
 *   getOrLoadAlbumCover: (albumSpec: object | null | undefined) => { status: string, image: HTMLImageElement } | null,
 *   clear: () => void
 * }} Cover image cache API.
 */
export function createCoverCache({ maxEntries = DEFAULT_MAX_CACHE_ENTRIES } = {}) {
  const entryLimit =
    typeof maxEntries === "number" && Number.isFinite(maxEntries) && maxEntries >= 1
      ? Math.floor(maxEntries)
      : DEFAULT_MAX_CACHE_ENTRIES;
  const coverMap = new Map();

  /**
   * Look up a cached cover entry or initiate an async image load for `albumSpec.coverUrl`.
   *
   * @param {object | null | undefined} albumSpec Album descriptor with optional `id` and `coverUrl`.
   * @returns {{ status: "loading" | "loaded" | "error", image: HTMLImageElement } | null} Cover entry or null.
   */
  function getOrLoadAlbumCover(albumSpec) {
    const coverUrl = typeof albumSpec?.coverUrl === "string" ? albumSpec.coverUrl.trim() : "";
    if (coverUrl.length === 0 || typeof Image !== "function") {
      return null;
    }

    const albumId = typeof albumSpec?.id === "string" && albumSpec.id.length > 0 ? albumSpec.id : "";
    const cacheKey = albumId.length > 0 ? `${albumId}|${coverUrl}` : coverUrl;

    if (coverMap.has(cacheKey)) {
      const cachedEntry = coverMap.get(cacheKey);
      coverMap.delete(cacheKey);
      coverMap.set(cacheKey, cachedEntry);
      return cachedEntry;
    }

    if (coverMap.size >= entryLimit) {
      const oldestKey = coverMap.keys().next().value;
      coverMap.delete(oldestKey);
    }

    const imageElement = new Image();
    imageElement.decoding = "async";
    const coverEntry = { status: "loading", image: imageElement };
    coverMap.set(cacheKey, coverEntry);

    imageElement.onload = () => {
      coverEntry.status = "loaded";
    };
    imageElement.onerror = () => {
      coverEntry.status = "error";
    };
    imageElement.src = coverUrl;

    return coverEntry;
  }

  /**
   * Evict all cached album artwork entries.
   */
  function clear() {
    coverMap.clear();
  }

  return {
    getOrLoadAlbumCover,
    clear,
  };
}
