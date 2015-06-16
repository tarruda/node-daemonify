var util = require('util');
var EventEmitter = require('events');

var rs = require('./remote_stream');
var MsgpackStream = require('./msgpack_stream');


function RemoteProcess(connection) {
  this.argv = null;
  this.cwd = null;
  this.exit = exit;
  var conn = new MsgpackStream(connection);
  var stdin = this.stdin = new rs.RemoteReadable(0, conn);
  var stdout = this.stdout = new rs.RemoteWritable(1, conn);
  var stderr = this.stderr = new rs.RemoteWritable(2, conn);
  var _this = this;

  conn.read(0);
  conn.once('readable', ready);
  connection.once('close', close);

  function ready() {
    var msg = conn.read();
    _this.argv = msg.argv;
    _this.cwd = msg.cwd;
    _this.emit('ready', _this);
  }

  var timeout;
  function exit(status) {
    console.error('invoked exit with status', status);
    conn.write([3, status || 0]);
    timeout = setTimeout(function() {
      console.error('remote failed ot close the connection in time');
      conn.end();
    }, 2000);
  }

  function close() {
    console.error('connection closed');
    if (timeout) clearTimeout(timeout);
  }
}
util.inherits(RemoteProcess, EventEmitter);

module.exports = RemoteProcess;
