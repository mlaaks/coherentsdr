clear all;
%handle to receiver:
sdr = CZMQSDR();

%one could also state this like below, but localhost is the default for the object
%sdr=CZMQSDR('IPAddress','127.0.0.1','Port','5555','ControlPort','5557');
sdr = CZMQSDR('IPAddress','192.168.0.1','Port','5555','ControlPort','5557');
Y = sdr();
