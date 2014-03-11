/**
 * @author John
 */
// Note: word vectors must be normalized to unit vectors before using them here.
var wordVectors = require('ltd/utils/wordVectors.json');
var cMath = require('varietyZoom/build/Release/cMath');
cMath.loadVecs(wordVectors);

var getWordVectors = function(event) {
  var text = event.ed;
  text = text.map(function(token) {return wordVectors[token];});
  text = text.filter(function(vector) {return vector;}).slice(0,3);
  return text;
};

// computes the matrix product of M1 with M2 transpose
// TODO: Settle on a matrix library or write a C extension, but make a decision one way or the other.
var dotT = function(M1, M2) {
  if (M1.length == 0 || M2.length == 0)
    return [];
  if (M1[0].length == 0 || M2[0].length == 0 || M1[0].length != M2[0].length)
    return undefined;
  var rows = M1.length;
  var cols = M2.length;
  var inner = M1[0].length;
  
  result = new Array(rows);
  for (var i=0; i < rows; ++i) {
    var row = new Array(cols);
    var src1 = M1[i];
    for (var j=0; j < cols; ++j) {
      var src2 = M2[j];
      var sum = 0.0;
      for (var k=0; k < inner; ++k)
        sum += src1[k]*src2[k];
      row[j] = sum;
    }
    result[i] = row;
  }
  
  return result;
};

var weirdCorrelationishMetric = function(wordVecs1, wordVecs2) {
  if (wordVecs1.length == 0 || wordVecs2.length == 0)
    return 1.0;
  
  var correls = dotT(wordVecs1, wordVecs2);
  
  // Now we have pairwise correlations between the vectors for each word in each event text.
  // This is where the somewhat arbitrary metric comes in.
  
  // Sum the maximum elements in each row
  var sum = 0.0;
  for (var i = 0; i < correls.length; ++i) {
    sum += Math.max.apply(null, correls[i]);
  }
  // Sum the maximum elements in each column
  var max = correls[0];
  for (var i = 1; i < correls.length; ++i)
    for (var j = 0; j < correls[i].length; ++j)
      max[j] = max[j] > correls[i][j] ? max[j] : correls[i][j];
  for (var j = 0; j < correls[0].length; ++j)
    sum +=  max[j];
    
  result = sum/(correls.length + correls[0].length);
  //cresult = cMath.wordMetric(wordVecs1, wordVecs2);
  return result;
};


var muV = 0.49475873081306054;
var sigV = 0.14822517233116092;
var muR = 0.5112155633954039;
var sigR = 0.14822517233116092;
var weirdMetricToDscore = function(metricValue) {
  // Apply a Fisher transformation so that the metric is approximately normally distributed.
  // And when I say approximately, we're talking spherical chickens in a vacuum.
  // The number slightly larger than 1 helps us not die when there's roundoff error in the metric calculation.
  metricValue = 0.5*Math.log((1.000000001 + metricValue)/(1.000000001 - metricValue));
  
  // The four magic numbers above are the normal distribution parameters for both events viewed (V) and rendered (R)
  // They came from fitting to data.
  // We use them to compute the change in log odds of a view, a.k.a. the change in score
  zView = (metricValue - muV)/sigV;
  zRender = (metricValue - muR)/sigR;
  return 0.5*zView*zView - 0.5*zRender*zRender + Math.log(sigR) - Math.log(sigV);
};

exports.sortWithVariety = function(list) {
  // Default .ed should be [] for production.
  list.forEach(function(event){event.ed = event.ed || [3580,12512,4770]; event.ed = event.ed.slice(0,3);event.wordVecs = getWordVectors(event);});
  
  var out = [];
  while (list.length > 0) {
    // remove highest-scoring event
    for (var i = 1; i < list.length; ++i)
      if (list[i-1].sortScore > list[i].sortScore) {
        // Always move better event toward the end so we can pop it like it's hot
        best = list[i-1];
        list[i-1] = list[i];
        list[i] = best;
      }
    best = list.pop();
    out.push(best);
    // rescore
    list.forEach(function(event){event.sortScore -= weirdMetricToDscore(weirdCorrelationishMetric(event.wordVecs, best.wordVecs));});
    //list.forEach(function(event){event.sortScore -= weirdMetricToDscore(cMath.wordMetric(event.ed, best.ed));});
  }
  return out;
};

// I am not going to carefully check every argument here. Make sure the argument to this function is:
// - An array
// - of objects
// - each of which has a sortScore and an ed
// - the sortScore is a double (NOT NaN, null or undefined)
// - ed is an array of ints (possibly empty but NOT null or undefined).
exports.sortWithCpp = function(list) {
  list.forEach(function(event){event.ed = event.ed || []; event.ed = event.ed.slice(0,3);});
  return cMath.sortEvents(list);
}
