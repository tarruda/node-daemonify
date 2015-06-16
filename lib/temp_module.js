var vm = require('vm');
var path = require('path');
var assert = require('assert');
var util = require('util');
var Module = require('module');

var has = require('has');


var natives = process.binding('natives');

function TempModule(id, parent) {
  Module.call(this, id, parent);
}
util.inherits(TempModule, Module);

TempModule.prototype.require = function(path) {
  assert(path, 'missing path');
  assert(typeof path === 'string', 'path must be a string');
  var filename = TempModule.resolveFilename(path, this);

  if (has(natives, filename)) {
    return require(filename);
  }

  return TempModule.load(filename, this).exports;
};

TempModule.prototype._compile = function(script, fname) {
  var _this = this;

  function require(path) {
    return _this.require(path);
  }
  require.resolve = function(request) {
    return Module._resolveFilename(request, _this);
  };

  require.extensions = Module._extensions;
  require.cache = this.cache;
  // create wrapped function
  var wrapped = Module.wrap(script.replace(/^\#\!.*/, ''));
  // run to instantiate the module
  return vm.runInThisContext(wrapped, {filename: fname}).apply(
    this.exports, [this.exports, require, this, fname, path.dirname(fname)]);
};

TempModule.resolveFilename = function(request, parent) {
  var filename;
  try {
    filename = Module._resolveFilename(request, parent);
  } catch (err) {
    var start;
    if (request.length >= 2
      && (start = request.substring(0, 2)) !== './'
      && start !== '..' && !has(natives, filename)) {
      try {
        // also try to load a script in the current directory by prepending
        // "./" to the requested module
        filename = Module._resolveFilename('./' + request, parent);
      } catch (err) {
        // failing that, try finding the script under the .daemonify directory
        // in the current directory.
        filename = Module._resolveFilename('./.daemonify/' + request, parent);
      }
    } else {
      throw err;
    }
  }
  return filename;
};

TempModule.load = function(filename, parent) {
  var cachedModule;

  if (parent)
    cachedModule = parent.cache[filename];

  if (cachedModule)
    return cachedModule;

  var module = new TempModule(filename, parent);

  if (!parent) {
    module.id = '.';
    module.cache = {};
  } else {
    module.cache = parent.cache;
  }

  module.cache[filename] = module;

  var hadException = true;

  try {
    module.load(filename);
    hadException = false;
  } finally {
    if (hadException) {
      delete module.cache[filename];
    }
  }

  return module;
};

module.exports = TempModule;
