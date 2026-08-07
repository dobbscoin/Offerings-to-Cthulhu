// Batch-translate R'lyehian verses to English using the site translator's own
// lexicon + decoder (contrib/site/rlyehian/). stdin: JSON array of strings.
// stdout: JSON array of English glosses (same order).
const fs = require('fs');
const path = require('path');
const RLY = path.join(__dirname, '..', 'site', 'rlyehian');
eval(fs.readFileSync(path.join(RLY, 'lexicon.js'), 'utf8').replace('const LEX', 'global.LEX'));
const t = require(path.join(RLY, 'translate.js'));
const verses = JSON.parse(fs.readFileSync(0, 'utf8'));
process.stdout.write(JSON.stringify(verses.map(v => {
  try { return t.decode(v).text; } catch (e) { return ''; }
})));
