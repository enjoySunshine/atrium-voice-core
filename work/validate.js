const fs = require('fs');
const data = fs.readFileSync('/workspace/work/report_data.js', 'utf8');
const checks = `
console.log('SR', SR, 'N', N, 'SWITCH', SWITCH, 'ALIGN_HALL', ALIGN_HALL, 'NBUF', NBUF);
console.log('PEAK_HALL_BEFORE', PEAK_HALL_BEFORE);
const keys = Object.keys(W);
console.log('channels:', keys.join(', '));
let bad = 0;
for (const k of keys) {
  if (!W[k].L || !W[k].R || W[k].L.mn.length !== NBUF || W[k].L.mx.length !== NBUF ||
      W[k].R.mn.length !== NBUF || W[k].R.mx.length !== NBUF) { console.log('BAD', k); bad++; }
}
function peakAbs(series, from, to) { let p = 0; for (let i = from; i < to; i++) { p = Math.max(p, Math.abs(series.mn[i]), Math.abs(series.mx[i])); } return p; }
const swB = Math.floor(SWITCH / BUCKET);
console.log('cave_dry peak (all):   ', peakAbs(W.cave_dry.L, 0, NBUF).toFixed(4));
console.log('hall_dry peak [0,sw):  ', peakAbs(W.hall_dry.L, 0, swB));
console.log('hall_dry peak [sw,end):', peakAbs(W.hall_dry.L, swB, NBUF).toFixed(4));
console.log('yard_dry peak:         ', peakAbs(W.yard_dry.L, 0, NBUF));
console.log('well_dry peak:         ', peakAbs(W.well_dry.L, 0, NBUF));
console.log('well_cho peak:         ', peakAbs(W.well_cho.L, 0, NBUF));
console.log('hall_rev peak [0,sw):  ', peakAbs(W.hall_rev.L, 0, swB));
console.log('cave_rev peak [sw,end):', peakAbs(W.cave_rev.L, swB, NBUF).toFixed(5));
console.log('ref_hall_dry peak[sw,):', peakAbs(W.ref_hall_dry.L, swB, NBUF).toFixed(4));
console.log(bad ? 'BAD CHANNELS: ' + bad : 'ALL CHANNELS OK');
`;
eval(data + checks);
