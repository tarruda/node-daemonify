var assert = require('assert');
var fs = require('fs');
var cp = require('child_process');
var path = require('path');
var EventEmitter = require('events');
var test = require('tape');

var has = require('has');
var through = require('through2');
var glob = require('glob');
var Promise = require('bluebird');

var daemonifyPath = path.resolve(
  path.join(__dirname, '..', 'build', 'Debug', 'daemonify'));
var socketName = 'test-' + ((Math.random() * 1000000000) | 0);
var daemonPath = path.resolve(path.join(__dirname, 'daemon.js'));

var tests = [];

glob.sync(path.join(__dirname, '*.yml')).forEach(function(yml) {
  tests = tests.concat(require('js-yaml').safeLoad(fs.readFileSync(yml)));
});

var useValgrind = process.env['VALGRIND'] && process.env['VALGRIND'] !== '0';
var valgrindLogPrefix = path.join(__dirname, 'daemonify-valgrind-');
var valgrindErrors = [];

function valgrindCheck(t) {
  var failed = [];
  glob.sync(valgrindLogPrefix + '*.log').forEach(function(reportFile) {
    var report = fs.readFileSync(reportFile, {encoding: 'utf8'});
    var match = /ERROR SUMMARY: (\d+) errors from \d+ contexts/.exec(report);
    if (match && parseInt(match[1]))
      failed.push(report);
    fs.unlinkSync(reportFile);
  });
  if (failed.length) {
    fs.writeFileSync('valgrind-report.log', failed.join('\n\n'), {
      encoding: 'utf8'
    });
    t.fail('valgrind detected errors, see valgrind-report.log for details');
  } else {
    t.pass('valgrind detected no errors');
  }
  t.end();
}

test('daemonify', {timeout: 50000}, function(t) {
  tests.forEach(function(te) {
    t.test(te.title, function(t) {
      var proc = new Proc(te.argv);
      var plan = 1;

      var promise = Promise.resolve();
      te.io.forEach(function(io) {
        var outKey = (io.stdout ? 'stdout' : io.stderr ? 'stderr' : null);
        if (outKey) {
          plan++;
          promise = promise.then(function() {
            return proc[outKey].expect(io[outKey]);
          }).then(function() {
            t.pass(outKey + ' matched "' + io[outKey] + '"');
          });
        } else if (has(io, 'stdin')) {
          promise = promise.then(function() {
            return proc.send(io.stdin);
          });
        }
      });

      t.plan(plan);
      promise = promise.then(function() {
        return proc.wait();
      }).then(function(status) {
        t.equal(status, te.status, 'status code matched "' + te.status + '"');
      });
    });
  })

  t.test('kill daemon', function(t) {
    t.plan(1);
    (new Proc(['-K'])).wait().then(function(status) {
      t.equal(status, 0, 'daemon killed successfully');
    });
  });

  if (useValgrind) t.test('check valgrind errors', valgrindCheck);
});


function Proc(argv, cwd) {
  cwd = cwd || path.join(__dirname, 'fixtures');
  var proc;
  argv = [
    '-N', process.execPath,
    '-L', socketName
  ].concat(argv);
  if (useValgrind) {
    proc = cp.spawn('valgrind', [
      '--log-file=' + valgrindLogPrefix + '%p.log',
      '--leak-check=yes',
      '--num-callers=50',
      '--error-exitcode=233',
      daemonifyPath
    ].concat(argv), {stdio: 'pipe', cwd: cwd});
  } else {
    proc = cp.spawn(daemonifyPath, argv, {stdio: 'pipe', cwd: cwd});
  }

  var exitResolve, exitStatus;

  proc.on('exit', function(status) {
    exitStatus = status;
    if (exitResolve != null)
      exitResolve(exitStatus);
  });

  this.wait = function wait() {
    return new Promise(function(resolve) {
      exitResolve = resolve;
      if (exitStatus != null)
        exitResolve(exitStatus);
    });
  };

  this.send = function send(data) {
    return new Promise(function(resolve) {
      if (data == null) return proc.stdin.end(function(err) {
        if (!err) resolve();
        else throw err;
      });
      proc.stdin.write(data, null, function(err) {
        if (!err) resolve();
        else throw err;
      });
    });
  };

  var stdoutEmitter = new EventEmitter();
  var stderrEmitter = new EventEmitter();
  var stdoutChunks = [];
  var stderrChunks = [];
  this.stdout = {expect: expect.bind(null, stdoutEmitter, stdoutChunks)};
  this.stderr = {expect: expect.bind(null, stderrEmitter, stderrChunks)};

  collect(proc.stdout, stdoutEmitter, stdoutChunks);
  collect(proc.stderr, stderrEmitter, stderrChunks);
}

function expect(emitter, actual, expected) {
  try {
    assert.equal(expected, actual.join(''));
    actual.splice(0, actual.length);
    return Promise.resolve();
  } catch (err) {}
  return new Promise(function(resolve) {
    function ondata() {
      try {
        assert.equal(expected, actual.join(''));
        // clear the list so the next assert will only consider new data
        actual.splice(0, actual.length);
        emitter.removeListener('data', ondata);
        resolve();
      } catch (err) {}
    }
    emitter.on('data', ondata);
  });
}

function collect(stream, emitter, target) {
  stream.on('data', function(data) {
    target.push(data.toString());
    emitter.emit('data');
  });
}
