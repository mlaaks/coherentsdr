classdef CZMQSDR < matlab.System
   properties
      CenterFrequency
   end
%    properties(PositiveInteger)
%       BlockLength=2^15;
%       NumChannels=2;
%    end
   properties(Nontunable)
      IPAddress ='127.0.0.1'; %'127.0.0.1', 5555
      Port = '5555';
      ControlPort = '5556';
   end
   methods
       function obj = CZMQSDR(varargin)
            setProperties(obj,nargin,varargin{:});
       end
   end
   methods(Access = protected)
      
       function validatePropertiesImpl(obj)
         
       end
      function processTunedPropertiesImpl(obj)
        if (obj.CenterFrequency<24e6) || (obj.CenterFrequency>1766e6)
            error('Property CenterFrequency out of range [24,1766] MHz.');
        end
          zmqsdr('t',obj.CenterFrequency); %retune (t)
      end
      
      function setupImpl(obj)
          zmqsdr('i',obj.CenterFrequency,obj.IPAddress,...
                     obj.Port,obj.ControlPort); % init (i)
      end
      function y=stepImpl(~)
          y=zmqsdr('r'); % receive (r)
      end
      function releaseImpl(~)
          zmqsdr('c'); % clear (c)
      end
   end
end