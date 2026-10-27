#!/bin/kjs
// lsirq - interrupt and exception counters per vector.
import * as kernel from 'kernel';
import hw from 'hw';
import fmt from 'fmt';

const counts = kernel.irqs();
const max = Math.max(...counts);
const rows = [];
counts.forEach((n, v) => {
    if (!n) return;
    rows.push([v === 48 ? '0x80' : v, hw.vectorName(v), n, fmt.bar(n / max, 24)]);
});
console.log(fmt.table(rows, { header: ['VEC', 'SOURCE', 'COUNT', ''], align: 'llr' }));
