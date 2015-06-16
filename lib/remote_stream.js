var util = require('util');

var Readable = require('readable-stream').Readable;
var Writable = require('readable-stream').Writable;


function RemoteWritable(fd, connection) {
  Writable.call(this);
  this.fd = fd;
  this.writable = true;
  this._remote = {connection: connection};
}
util.inherits(RemoteWritable, Writable);

RemoteWritable.prototype.end = function(chunk, enc, cb) {
  var conn = this._remote.connection;
  var fd = this.fd;
  var _this = this;

  function sendEof(err) {
    if (err) return cb(err);
    conn.write([fd, null], null, function(err) {
      if (err) return cb(err);
      Writable.end.call(_this, null, null, cb);
    });
  }

  if (chunk) {
    this.write(chunk, enc, sendEof);
  } else {
    sendEof();
  }
};

RemoteWritable.prototype._write = function(data, enc, cb) {
  var msg = [this.fd, enc ? new Buffer(data, enc) : data];
  this._remote.connection.write(msg, null, cb);
};


function RemoteReadable(fd, connection) {
  Readable.call(this);
  this.fd = fd;
  this._remote = {connection: connection};
}
util.inherits(RemoteReadable, Readable);

RemoteReadable.prototype._read = function(size) {
  var conn = this._remote.connection;
  var _this = this;
  return fetchMessage();

  function fetchMessage() {
    var msg = conn.read();

    if (!msg) {
      conn.read(0);
      return conn.once('readable', fetchMessage);
    }

    if (msg[0] !== _this.fd) {
      _this.emit('message', msg);
    } else if (msg[1] === null) {
      _this.push(null);
      _this.unpipe();
    } else {
      _this.push(msg[1]);
    }
  }
};


exports.RemoteWritable = RemoteWritable;
exports.RemoteReadable = RemoteReadable;
