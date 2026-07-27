// Smoke tests for the translator engine. Run: node test_translate.js
const fs = require('fs');
const path = require('path');
eval(fs.readFileSync(path.join(__dirname, 'lexicon.js'), 'utf8').replace('const LEX', 'global.LEX'));
const R = require('./translate.js');
R.setLexicon(global.LEX);

function show(label, r) {
  console.log('== ' + label);
  console.log('   ' + r.text);
}

show('chant decode', R.decode("Ph'nglui mglw'nafh Cthulhu R'lyeh wgah'nagl fhtagn"));
show('chant curly-quotes', R.decode('Ph’nglui mglw’nafh Cthulhu R’lyeh wgah’nagl fhtagn'));
show('bound form (shatters on lingojam)', R.decode("Ph'nglui mglw'nafh Cthulhu R'lyeh ng fhtagn"));
show('vesper', R.decode("Vulgtmoth n'ghftyar, gnaiih. Mghlirgh gof'nn nnn, syha'h. Nog ephaii, yogfm'll; nog ephaii, ahair'luh. Y'hah."));

console.log('== divergence words');
["fhtagn", "uaaah", "syha'h", "vulgtmnah", "zhro", "ilyaa", "kadishtu", "throd", "k'yarnak", "ep", "goka", "nog", "gn'th", "ehye"].forEach(function (w) {
  const r = R.decode(w);
  const s = r.segments[0];
  console.log('   ' + w.padEnd(12) + ' -> ' + r.text.padEnd(16) + ' [' + s.prov + (s.chain ? ' +chain' : '') + '] ' +
    s.senses.map(x => x.from + ': ' + x.s).join(' | '));
});

console.log('== morphology guesses');
["mgepbug", "cthulhunyth", "shaggoth", "naflfhtagn", "gof'nn", "uh'ee"].forEach(function (w) {
  const r = R.decode(w);
  const s = r.segments[0];
  console.log('   ' + w.padEnd(12) + ' -> ' + r.text.padEnd(20) + ' [' + s.kind + '] ' + (s.note || ''));
});

show('encode chant', R.encode("In his house at R'lyeh dead Cthulhu dreams"));
show('encode simple', R.encode('The children of the deep pray to Cthulhu and wait'));
show('encode unknown word', R.encode('the blockchain waits eternally'));
show('encode amen', R.encode('amen'));
show('roundtrip: encode->decode', R.decode(R.encode('the faithful children watch the stars, always').text));
