var split = require('split');
var through = require('through2');

exports.exec = function(process, console) {
  process.stdin
  .pipe(split())
  .pipe(through(function(line, enc, next) {
    line = line.toString();
    if (line === 'exit') {
      console.log('exit');
      process.exit(0);
    } else if (line === 'error') {
      console.error('error');
      process.exit(1);
    } else {
      console.log('echo:', line);
    }
    next();
  }));

  process.stdin.on('end', function() {
    console.log('stdin closed!');
    process.exit(0);
  });
};
