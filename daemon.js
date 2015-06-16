var net = require('net');
var fs = require('fs');
var path = require('path');
var fork = require('child_process').fork;
var Console = require('console').Console;

var has = require('has');

var RemoteProcess = require('./lib/remote_process');
var TempModule = require('./lib/temp_module');

var sock = process.argv[process.argv.length - 2];
var pidfile = process.argv[process.argv.length - 1];

if (fs.existsSync(pidfile)) {
  console.error('pid file "' + pidfile + '" already exists, remove it manually');
  process.exit(1);
}

var workers = {};
process.chdir('/');

var server = net.createServer(function(connection) {
  var proc = new RemoteProcess(connection);
  var moduleName;

  proc.once('ready', function(p) {
    try {
      process.chdir(p.cwd);
      var filename = TempModule.resolveFilename(p.argv[0]);
      var worker;
      var exports;
      if (!has(workers, filename)) {
        worker = TempModule.load(filename, null);
        worker.cachedExports = worker.exports;
        if (typeof worker.cachedExports === 'function')
          worker.cachedExports = worker.cachedExports();
        workers[filename] = worker;
      } else {
        worker = workers[filename];
      }

      if (worker.timer) {
        clearTimeout(worker.timer);
      }

      worker.timer = setTimeout(function() {
        delete workers[filename];
      }, worker.cachedExports.timeout || 60 * 60 * 1000);

      worker.cachedExports.exec(p, new Console(p.stdout, p.stderr));
    } catch (err) {
      connection.end();
    } finally {
      process.chdir('/');
    }
  });

}).listen(sock);

['SIGTERM', 'SIGINT'].forEach(function(sig) {
  process.on(sig, function() {
    console.error('received ' + sig + ', exiting');
    process.exit(0);
  });
});

process.on('exit', function() {
  if (fs.existsSync(pidfile)) fs.unlinkSync(pidfile);
  server.close();
});

console.error('listening on', sock);
fs.writeFileSync(pidfile, process.pid.toString());
console.error('pid file ', pidfile);
