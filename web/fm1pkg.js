// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 package (.fwsc) in the browser: the same reading and checks as tools/fm1_install.py (product_of,
// logical_image, deinterleave, package_damage). tests/install_test.py compares logicalImage / productOf with the
// Python on build/drumtonic.fwsc; web/test_web.mjs runs the rest. No DOM, no MIDI: node imports it as it is, and
// tools/make_installer.py inlines it into the installer page (the `export` words dropped).

export const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F, MAX_BLOCKS = 64;
export const LOADER_MARK = "FELUCCA-LOADER-1";

const idChar = (m, i) => { const c = (m - i - 1) & 0xFF; return c >= 0x20 && c < 0x7F; };
const marker = (raw, k) => ((k + 1) * BLK <= raw.length ? raw[k * BLK + KEEP] : null);

function identityBlocks(raw) {
    // 20, or more when the identity is longer: further identity markers count only when unused 0x7D markers follow
    let n = BLOCKS, m;
    while (n < MAX_BLOCKS && (m = marker(raw, n)) !== null && m !== 0x7D && idChar(m, n)) n++;
    return n > BLOCKS && marker(raw, n) === 0x7D ? n : BLOCKS;
}

function blockCount(raw) {
    let n = identityBlocks(raw);
    while (n < MAX_BLOCKS && (n + 1) * BLK <= raw.length && raw[n * BLK + KEEP] === 0x7D) n++;
    return n;
}

/** the image the device reads during an update: the package without the marker bytes */
export function logicalImage(raw, blocks = BLOCKS) {
    const L = raw.length, at = x => Math.min(x, L), parts = [];
    for (let i = 0; i < blocks; i++) parts.push(raw.subarray(at(i * BLK), at(i * BLK + KEEP)));
    parts.push(raw.subarray(at(blocks * BLK)));
    const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
    let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    return out;
}

/** -> [logical image, block count]: the detected count when the files' CRCs agree, else 20 */
export function deinterleave(raw) {
    const n = blockCount(raw), image = logicalImage(raw, n);
    if (n !== BLOCKS && packageDamage(image)) {
        const alt = logicalImage(raw, BLOCKS);
        if (!packageDamage(alt)) return [alt, BLOCKS];
    }
    return [image, n];
}

/** the package identity ("FM-1_933"): one marker byte after each identity block (0x7D: none) */
export function productOf(raw) {
    if (raw.length < BLOCKS * BLK) throw new Error("not an FM-1 package (too short)");
    const ib = identityBlocks(raw);
    const n = ib === BLOCKS ? BLOCKS : Math.min(ib, deinterleave(raw)[1]);
    let s = "";
    for (let i = 0; i < n; i++) { const m = raw[i * BLK + KEEP]; if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF); }
    return s;
}

export function crc16(data, crc = 0) {      // CRC-16/XMODEM, as the package and the device use
    for (let i = 0; i < data.length; i++) {
        crc ^= data[i] << 8;
        for (let k = 0; k < 8; k++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
    }
    return crc;
}

function dec(b) {                           // JieLi ENC stream cipher (its own inverse)
    const out = Uint8Array.from(b);
    let key = 0xFFFF;
    for (let i = 0; i < out.length; i++) {
        out[i] ^= key & 0xFF;
        key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
    }
    return out;
}

const u16 = (b, o) => b[o] | b[o + 1] << 8;
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;

/** what is wrong with the update image (header, file list, each file's CRC), or null */
export function packageDamage(image) {
    if (image.length < 0x40) return "too short";
    const hdr = dec(image.subarray(0, 0x40));
    if (crc16(hdr.subarray(2, 0x40)) !== u16(hdr, 0)) return "header CRC";
    const count = u16(hdr, 8);
    if (count < 1 || count > 8 || image.length < 0x40 + 0x50 * count) return "file list";
    const lst = image.subarray(0x40, 0x40 + 0x50 * count);
    if (crc16(lst) !== u16(hdr, 2)) return "file list CRC";
    for (let i = 0; i < count; i++) {
        const e = dec(lst.subarray(i * 0x50, (i + 1) * 0x50));
        const off = u32(e, 8), size = u32(e, 12), typ = u16(e, 0);
        let name = "";
        for (let k = 0x40; k < 0x50 && e[k]; k++) name += String.fromCharCode(e[k]);
        if (off + size > image.length) return name + ": cut short";
        // flash.bin (0) and ota.bin (100) carry the CRC of their bytes as served; the other entries only their bounds
        if ((typ === 0 || typ === 100) && crc16(image.subarray(off, off + size)) !== u16(e, 4)) return name + ": CRC";
    }
    return null;
}

function hasMark(raw) {
    const m = Array.from(LOADER_MARK, c => c.charCodeAt(0));
    outer: for (let i = 0; i + m.length <= raw.length; i++) {
        for (let k = 0; k < m.length; k++) if (raw[i + k] !== m[k]) continue outer;
        return true;
    }
    return false;
}

/** the checks before any MIDI: -> {product, image} or throws an Error whose message says what is wrong */
export function loadPackage(raw, want) {
    const product = productOf(raw);
    if (!/^[^_]+_\d+$/.test(product)) throw new Error(`not an FM-1 package (identity ${product})`);
    if (want && product !== want) throw new Error(`the package is ${product}, not ${want}`);
    if (!hasMark(raw)) throw new Error("no Felucca update loader in this package");
    const [image] = deinterleave(raw);
    const bad = packageDamage(image);
    if (bad) throw new Error(`damaged package (${bad})`);
    return { product, image };
}
