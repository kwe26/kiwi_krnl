// kz: the kernel's memory compression (zram + compressed ramfs).
import * as sys from 'sys';

// raw kernel counters plus derived figures (all sizes in bytes)
export function stats() {
    const z = sys.kzstat();
    const page = z.page_size;
    const zOrig = z.z_pages * page;
    return {
        ...z,
        ram: z.total_frames * page,
        free: z.free_frames * page,
        resident: z.user_resident * page,
        budgetBytes: z.budget * page,
        zOrig,
        zSaved: Math.max(0, zOrig - z.z_bytes),
        zRatio: z.z_bytes ? zOrig / z.z_bytes : 0,
        fsSaved: Math.max(0, z.fs_bytes - z.fs_zbytes),
        fsRatio: z.fs_zbytes ? z.fs_bytes / z.fs_zbytes : 0,
    };
}

export const setBudget = (mb) => sys.kzctl(sys.KZ_SET_BUDGET, Math.round(mb * 256));
export const getBudget = () => sys.kzctl(sys.KZ_GET_BUDGET, 0);
export const compressAll = () => sys.kzctl(sys.KZ_COMPRESS_ALL, 0);
export const setProactive = (scans) => sys.kzctl(sys.KZ_SET_PROACTIVE, scans);

export default { stats, setBudget, getBudget, compressAll, setProactive };
