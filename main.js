/**
 * @author John
 */
var scoring = require('./scoring.js');
var encoder = require('./resources/encoder.json');

console.log("Sort for three samples. The highest score is at the top. The next two scores are close, so variety makes the item less similar to the top item come next.");
console.log(scoring.sortWithCpp([{ed:[1,2,3,4,5],sortScore:0.1},{ed:[2,7,4,6],sortScore:-0.5},{ed:[20011,16514,2],sortScore:-0.5}]));
