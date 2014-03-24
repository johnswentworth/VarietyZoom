#include <node.h>
#include <v8.h>
#include <vector>
#include <string>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <thread>

// I am expecting my CS prof Melissa to rappel in through the skylight and kill me for this...
using namespace v8;
using namespace std;

// ... and she'll make it REALLY slow and painful for this.
vector<double*> wordVecs;

// VERY rough sheets.
int threadCount = 4;

// This function is called once at startup to load the wordVectors into that charming global variable.
Handle<Value> loadVecs(const Arguments& args) {
  HandleScope scope;
  
  Local<Array> rawWordVecs = Local<Array>::Cast(args[0]);
  
  int words = rawWordVecs->Length();
  if (words == 0)
    return scope.Close(String::New("Nope!"));
  
  wordVecs = vector<double*>();
  
  for (int i = 0; i < words; ++i) {
    Local<Array> row = Local<Array>::Cast(rawWordVecs->CloneElementAt(i));
    double* vec = new double[300];
    for (int j = 0; j < 300; ++j)
      vec[j] = row->Get(j)->ToNumber()->Value();
    wordVecs.push_back(vec);
  }
  cout << "Loaded " << wordVecs.size() << " wordVectors!\n";
  return scope.Close(String::New("Locked and loaded."));

}

/*
General outline:
  Four functions are exposed to node. The first, above, is simply for loading 
  the word vectors and should be called before anything else.
  
  The second is wordMetric, which takes words for two events represented as
  indices in the word vector list. It then returns a blatantly silly ad-hoc
  metric of similarity between the two events, for which 1.0 represents a
  perfect match (plus or minus error). The metric is implemented in a function
  conveniently called "metric".
  
  The third is sortEvents, which handles sorting with variety. Using sortEvents
  saves overhead compared to calling wordMetric and performing the sort in js.
  Overhead in calling sortEvents is negligible; 90% of the runtime is spent
  computing the dot products which are the core of this whole disaster. sortEvents
  creates and sorts a vector of SimpleEvents. 
  
  On each step, the sort algorithm takes the high-scoring event and adds it to the
  result list. All remaining events are then rescored so that events similar to
  the high-scoring event are moved down, thus promoting variety. This method is
  unavoidably O(n^2), which is why it's in C++.
  
  The fourth function is scoreInterest, which recores the first array of events
  based on user interest in the second array of events.
*/

vector<int> getIntArray(Local<Array> intArray) {
  int size = intArray->Length();
  vector<int> result = vector<int>();
  result.reserve(size);
  for (int i = 0; i < size; ++i)
    result.push_back(intArray->Get(i)->ToInteger()->Value());
  return result;
}

double dot(double* v1, double* v2, int size) {
  double result = 0.0;
  for (int i = 0; i < size; ++i)
    result += v1[i]*v2[i];
  return result;
}

double metric(vector<int>& words1, vector<int>& words2) {
  int rows = words1.size();
  int cols = words2.size();
  int correlSize = rows*cols;
  double* correls = new double[correlSize];
  int rowInd = 0;
  for (int i = 0; i < words1.size(); ++i) {
    for (int j = 0; j < words2.size(); ++j) {
      // This line is where the action happens. It is a little more than 90% of the runtime.
      correls[rowInd + j] = dot(wordVecs[words1[i]], wordVecs[words2[j]], 300);
    }
    rowInd += cols;
  }
  
  double metricVal = 0.0;
  double max;
  // sum max for each row
  for (int rowInd = 0; rowInd < correlSize; rowInd += rows) {
    max = -1.0;
    for (int j = 0; j < cols; ++j)
      max = correls[rowInd + j] > max ? correls[rowInd + j] : max;
    metricVal += max;
  }
  // sum max for each col
  for (int j = 0; j < cols; ++j) {
    max = -1.0;
    for (int rowInd = 0; rowInd < correlSize; rowInd += rows)
      max = correls[rowInd + j] > max ? correls[rowInd + j] : max;
    metricVal += max;
  }
  
  delete[] correls;
  
  return metricVal/(rows+cols);
}

class SimpleEvent {
  public:
    vector<int> words;
    double sortScore;
    int srcIndex;
  
  // It is caller's responsibility to make sure the neccessary fields are present.
  SimpleEvent(Local<Object> jsEvent, int index) {
    sortScore = jsEvent->Get(String::New("sortScore"))->ToNumber()->Value();
    words = getIntArray(Local<Array>::Cast(jsEvent->Get(String::New("ed"))));
    srcIndex = index;
  }
  
  static constexpr double muV = 0.49475873081306054;
  static constexpr double sigV = 0.14822517233116092;
  static constexpr double muR = 0.5112155633954039;
  static constexpr double sigR = 0.14822517233116092;
  
  static double weirdMetricToDscore(double metricValue) {
    // Apply a Fisher transformation so that the metric is approximately normally distributed.
    // And when I say approximately, we're talking spherical chickens in a vacuum.
    // The number slightly larger than 1 helps us not die when there's roundoff error in the metric calculation.
    metricValue = 0.5*log((1.000000001 + metricValue)/(1.000000001 - metricValue));
  
    // The four magic numbers above are the normal distribution parameters for both events viewed (V) and rendered (R)
    // They came from fitting to data.
    // We use them to compute the change in log odds of a view, a.k.a. the change in score
    double zView = (metricValue - muV)/sigV;
    double zRender = (metricValue - muR)/sigR;
    return 0.5*zView*zView - 0.5*zRender*zRender + log(sigR) - log(sigV);
  }
  
  static constexpr double muVS = 0.39572821759080429;
  static constexpr double sigVS = 0.13549539232977462;//0.15290152697711662;
  static constexpr double muRS = 0.35188236872960243;
  static constexpr double sigRS = 0.13549539232977462;

  static double weirdMetricToDscoreInterest(double metricValue) {
    metricValue = 0.5*log((1.000000001 + metricValue)/(1.000000001 - metricValue));
    
    double zView = (metricValue - muVS)/sigVS;
    double zRender = (metricValue - muRS)/sigRS;
    return 0.5*zView*zView - 0.5*zRender*zRender + log(sigRS) - log(sigVS);
  }
  
  void rescore(SimpleEvent& other) {
    if (words.size() == 0 || other.words.size() == 0) // If there is no description, assume the worst.
      sortScore -= weirdMetricToDscore(1.0);
    else
      sortScore -= weirdMetricToDscore(metric(words, other.words));
  }
  
  void incorporateInterest(SimpleEvent& other) {
    if (words.size() == 0 || other.words.size() == 0) // If there is no description, assume the worst.
      sortScore -= weirdMetricToDscoreInterest(0.0);
    else
      sortScore -= weirdMetricToDscoreInterest(metric(words, other.words));
  }
  
  void swap(SimpleEvent& other) {
    words.swap(other.words);
    
    double tmpScore = sortScore;
    sortScore = other.sortScore;
    other.sortScore = tmpScore;
    
    // Just to mess with the interns ;)
    srcIndex ^= other.srcIndex;
    other.srcIndex ^= srcIndex;
    srcIndex ^= other.srcIndex;
  }
};

Handle<Value> wordMetric(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 2) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }

  if (!args[0]->IsArray() || !args[1]->IsArray()) {
    ThrowException(Exception::TypeError(String::New("Wrong arguments")));
    return scope.Close(Undefined());
  }
  
  vector<int> words1 = getIntArray(Local<Array>::Cast(args[0]));
  vector<int> words2 = getIntArray(Local<Array>::Cast(args[1]));
  
  if (words1.size() == 0 || words2.size() == 0)
    return scope.Close(String::New("Nope!"));
  
  double metricVal = metric(words1, words2);
  
  return scope.Close(Number::New(metricVal));
}

// I am not going to carefully check every argument here. Make sure the first argument to this function is:
// - An array
// - of objects
// - each of which has a sortScore and an ed
// - the sortScore is a double (NOT NaN, null or undefined)
// - ed is an array of ints (possibly empty but NOT null or undefined).
Handle<Value> sortEvents(const Arguments& args) {
  HandleScope scope;
  
  Local<Array> jsEvents = Local<Array>::Cast(args[0]);
  int numEvents = jsEvents->Length();
  vector<SimpleEvent> events = vector<SimpleEvent>();
  events.reserve(numEvents);
  for (int i = 0; i < numEvents; ++i)
    events.push_back(SimpleEvent(jsEvents->CloneElementAt(i), i));
  
  Local<Array> result = Array::New();
  SimpleEvent* best;
  while (events.size() > 0) {
    for (int i = 1; i < events.size(); ++i)
      if (events[i-1].sortScore > events[i].sortScore) {
        // Always move better event toward the end so we can pop it like it's hot
        events[i-1].swap(events[i]);
      }
    // rescore
    best = & events[events.size()-1];
    
    // This is where variety happens. At least 95% of the runtime is from this loop. Without it, runtime drops to < 1 ms, even with the horrible bubble sort.
    for (int i = 0; i < events.size(); ++i) {
      events[i].rescore(* best);
    }
    result->Set(result->Length(), jsEvents->Get(best->srcIndex));
    events.pop_back();
  }
  return scope.Close(result);
}

void processEvents(vector<SimpleEvent> events, vector<SimpleEvent> interestedEvents, int startIndex, int endIndex) {
  int numInterestedEvents = interestedEvents.size();
  for (int i = startIndex; i < endIndex; ++i) {
    SimpleEvent event = events[i];
    for (int j = 0; j < numInterestedEvents; ++j)
      event.incorporateInterest(interestedEvents[j]);
  }
}

// Handy for debugging
void doNothing() {
  cout << "I'm in a thread!\n";
}

// Make sure the first argument to this function is:
// - An array
// - of objects
// - each of which has a sortScore and an ed
// - the sortScore is a double (NOT NaN, null or undefined)
// - ed is an array of ints (possibly empty but NOT null or undefined).
// and the second argument is:
// - An array
// - of objects
// - each of which has an ed
// - ed is an array of ints (possibly empty but NOT null or undefined).
Handle<Value> scoreInterest(const Arguments& args) {
  HandleScope scope;
  
  Local<Array> jsEvents = Local<Array>::Cast(args[0]);
  int numEvents = jsEvents->Length();
  vector<SimpleEvent> events = vector<SimpleEvent>();
  events.reserve(numEvents);
  for (int i = 0; i < numEvents; ++i)
    events.push_back(SimpleEvent(jsEvents->CloneElementAt(i), i));
  
  Local<Array> jsInterestedEvents = Local<Array>::Cast(args[1]);
  int numInterestedEvents = jsInterestedEvents->Length();
  vector<SimpleEvent> interestedEvents = vector<SimpleEvent>();
  interestedEvents.reserve(numInterestedEvents);
  for (int i = 0; i < numInterestedEvents; ++i)
    interestedEvents.push_back(SimpleEvent(jsInterestedEvents->CloneElementAt(i), i));
  
  // The non-threaded version
  /*for (int i = 0; i < numEvents; ++i) {
    SimpleEvent event = events[i];
    for (int j = 0; j < numInterestedEvents; ++j)
      event.incorporateInterest(interestedEvents[j]);
  }*/
  
  // Proof of concept: threads can get along with node w/o using libuv.
  //thread worker(doNothing);
  //worker.join();
  
  int chunkSize = numEvents/threadCount;
  
  vector<thread> workers = vector<thread>();
  for (int t = 0; t < threadCount - 1; ++t) {
    workers.push_back(thread(processEvents, events, interestedEvents, chunkSize*t, chunkSize*t+chunkSize));
  }
  // Main thread handles a few extra events plus its share.
  for (int i = chunkSize * (threadCount - 1); i < numEvents; ++i) {
    Local<Object> jsEvent  = jsEvents->Get(i)->ToObject();
    SimpleEvent event = SimpleEvent(jsEvent, i);
    for (int j = 0; j < numInterestedEvents; ++j)
      event.incorporateInterest(interestedEvents[j]);
    jsEvent->Set(String::New("sortScore"), Number::New(event.sortScore));
  }
  for (int t = 0; t < threadCount - 1; ++t)
    if (workers[t].joinable())
      workers[t].join();
  
  for (int i = 0; i < numEvents; ++i) {
    Local<Object> jsEvent  = jsEvents->Get(i)->ToObject();
    jsEvent->Set(String::New("sortScore"), Number::New(events[i].sortScore));
  }
  
  return scope.Close(String::New("These are not the events you are looking for."));
}

// Make sure the first argument to this function is:
// - An array
// - of objects
// - each of which has a sortScore and an ed
// - the sortScore is a double (NOT NaN, null or undefined)
// - ed is an array of ints (possibly empty but NOT null or undefined).
// and the second argument is:
// - An array
// - of objects
// - each of which has an ed
// - ed is an array of ints (possibly empty but NOT null or undefined).
Handle<Value> scoreUninterest(const Arguments& args) {
  HandleScope scope;
  
  Local<Array> jsEvents = Local<Array>::Cast(args[0]);
  int numEvents = jsEvents->Length();
  vector<SimpleEvent> events = vector<SimpleEvent>();
  events.reserve(numEvents);
  for (int i = 0; i < numEvents; ++i)
    events.push_back(SimpleEvent(jsEvents->CloneElementAt(i), i));
  
  Local<Array> jsUninterestedEvents = Local<Array>::Cast(args[1]);
  int numInterestedEvents = jsUninterestedEvents->Length();
  vector<SimpleEvent> interestedEvents = vector<SimpleEvent>();
  interestedEvents.reserve(numInterestedEvents);
  for (int i = 0; i < numInterestedEvents; ++i)
    interestedEvents.push_back(SimpleEvent(jsUninterestedEvents->CloneElementAt(i), i));
  
  // The non-threaded version
  for (int i = 0; i < numEvents; ++i) {
    SimpleEvent event = events[i];
    for (int j = 0; j < numInterestedEvents; ++j)
      event.rescore(interestedEvents[j]);
  }
  
  for (int i = 0; i < numEvents; ++i) {
    Local<Object> jsEvent  = jsEvents->Get(i)->ToObject();
    jsEvent->Set(String::New("sortScore"), Number::New(events[i].sortScore));
  }
  
  return scope.Close(String::New("These are not the events you are looking for."));
}

// node.js magic
void init(Handle<Object> exports) {
  exports->Set(String::NewSymbol("loadVecs"),
      FunctionTemplate::New(loadVecs)->GetFunction());
  exports->Set(String::NewSymbol("wordMetric"),
      FunctionTemplate::New(wordMetric)->GetFunction());
  exports->Set(String::NewSymbol("sortEvents"),
      FunctionTemplate::New(sortEvents)->GetFunction());
  exports->Set(String::NewSymbol("scoreInterest"),
      FunctionTemplate::New(scoreInterest)->GetFunction());
  exports->Set(String::NewSymbol("scoreUninterest"),
      FunctionTemplate::New(scoreUninterest)->GetFunction());
}

NODE_MODULE(cMath, init)
