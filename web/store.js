// IndexedDB storage: the player's ROM (it never leaves the browser) and
// recompilation results, keyed by everything they depend on.

const DB_NAME = 'smb-recompiled';
const STORES = ['rom', 'builds'];

function open() {
    return new Promise((resolve, reject) => {
        const request = indexedDB.open(DB_NAME, 1);
        request.onupgradeneeded = () => {
            for (const name of STORES) {
                if (!request.result.objectStoreNames.contains(name)) request.result.createObjectStore(name);
            }
        };
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error);
    });
}

async function transaction(store, mode, action) {
    const db = await open();
    return new Promise((resolve, reject) => {
        const tx = db.transaction(store, mode);
        const request = action(tx.objectStore(store));
        tx.oncomplete = () => resolve(request.result);
        tx.onerror = () => reject(tx.error);
    });
}

export const get = (store, key) => transaction(store, 'readonly', s => s.get(key)).catch(() => undefined);
export const put = (store, key, value) => transaction(store, 'readwrite', s => s.put(value, key)).catch(() => undefined);
export const clear = store => transaction(store, 'readwrite', s => s.clear()).catch(() => undefined);

// SHA-256 of strings and byte arrays, as hex.
export async function digest(...parts) {
    const encoder = new TextEncoder();
    const chunks = parts.map(part => typeof part === 'string' ? encoder.encode(part) : part);
    const total = new Uint8Array(chunks.reduce((n, c) => n + c.length + 4, 0));
    let offset = 0;
    for (const chunk of chunks) {
        new DataView(total.buffer).setUint32(offset, chunk.length);
        total.set(chunk, offset + 4);
        offset += chunk.length + 4;
    }
    const hash = new Uint8Array(await crypto.subtle.digest('SHA-256', total));
    return Array.from(hash, b => b.toString(16).padStart(2, '0')).join('');
}
