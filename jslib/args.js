// args: small getopt-style command line parser.
//
//   const o = parse(argv, { boolean: 'lan', string: 'n', alias: { n: 'lines' } });
//   o.l, o.n, o._ (positional), '--' ends options.
//   spec.numeric: 'n' makes "-5" mean "-n 5" (head, tail).
export function parse(argv, spec = {}) {
    const bools = new Set(typeof spec.boolean === 'string' ? spec.boolean.split('') : spec.boolean || []);
    const strs = new Set(typeof spec.string === 'string' ? spec.string.split('') : spec.string || []);
    const alias = spec.alias || {};
    const out = { _: [] };
    for (const b of bools) out[b] = false;
    Object.assign(out, spec.defaults || {});
    const set = (k, v) => {
        out[k] = v;
        if (alias[k]) out[alias[k]] = v;
        for (const [a, b] of Object.entries(alias)) if (b === k) out[a] = v;
    };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--') { out._.push(...argv.slice(i + 1)); break; }
        if (a.startsWith('--') && a.length > 2) {
            const eq = a.indexOf('=');
            const key = eq > 0 ? a.slice(2, eq) : a.slice(2);
            if (eq > 0) set(key, a.slice(eq + 1));
            else if (strs.has(key) && i + 1 < argv.length) set(key, argv[++i]);
            else set(key, true);
        } else if (spec.numeric && /^-\d+$/.test(a)) {
            set(spec.numeric, a.slice(1));
        } else if (a.startsWith('-') && a.length > 1 && !/^-\d/.test(a)) {
            for (let j = 1; j < a.length; j++) {
                const k = a[j];
                if (strs.has(k)) {
                    const rest = a.slice(j + 1);
                    set(k, rest || argv[++i]);
                    break;
                }
                if (!bools.has(k) && spec.strict) throw new Error(`unknown option -${k}`);
                set(k, true);
            }
        } else {
            out._.push(a);
        }
    }
    return out;
}

export default { parse };
