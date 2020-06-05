# obs-loopback

This module outputs (aka streams) localy.

OBS streaming redirected to: 
- loopback video device (/dev/video10  for example)
- pulse audio pipe sinc (/tmp/obs-pa-pipe for eaxmple)

This names configured in special item of the services.json file
where all other stream's target are.

Location of this file (services.json) varies from installtions
and can be in:
- $HOME/.config/obs/obs-plugins/rtmp-services/services.json
- /usr/share/obs/obs-plugins/rtmp-services/services.json
or, in the case of a portable version
- _INSTALL_DIR_/data/obs-plugins/rtmp-services/services.json
where _INSTALL_DIR_ is the directory OBS installed to
(i.e. folder _INSTALL_DIR_/bin exists)


then we should _patch_ this file including special an our streaming service
like this:

```json
{
....... OTHER serices items ....

    "services": [
    	{
    		"name": "LOOPBACK",
    		"common": true,
    		"servers": [ {
    			"name": "default",
    			"url" : "/dev/video10:/tmp/obs-pa-pipe"
    		}]
    		,
            "recommended": {
                "output": "LoopbackSink"
            }
    	},
    	
....... OTHER serices items ....

}
```

Then

Make the loopback video device:

```
modprobe v4l2loopback video_nr=10 card_label="YOUR-VIRTUAL-CAMERA-NAME"
```
replace 10 with number as you want, and correspended with 
the number in *url* item in the service file

```
pactl load-module module-pipe-source file=/tmp/obs-pa-pipe channels=1 source_name=OBS_pipe source_properties=device.description=YOUR-VIRTUAL-MIC-NAME
```
also replace /tmp/obs-pa-pipe with path as you want, and correspended with 
the path in *url* item in the service file

starting video loopback device and audio pipe can be done 
at user session startup via corresponging system/shell user setting
which are different among distributions and desktop enviromets

### DONE
Now we can select our *LOOPBACK* service in the streaming settings
and Start Streaming

and use
_YOUR-VIRTUAL-CAMERA-NAME_ as a camera in zoom/skype etc
and
_YOUR-VIRTUAL-MIC-NAME_ as a microphone there

## Build

- Get obs-studio source code

```
git clone --recursive https://github.com/obsproject/obs-studio.git
```

- Build plugins

```
git clone https://github.com/andy7d0/obs-loopback.git
cd obs-loopback
mkdir build && cd build
cmake -DLIBOBS_INCLUDE_DIR="../../obs-studio/libobs" -DCMAKE_INSTALL_PREFIX=/usr ..
make -j4
sudo make install
```
