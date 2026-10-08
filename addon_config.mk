meta:
	ADDON_NAME = ofxOceanodeForms
	ADDON_DESCRIPTION = FORMS / Scroller nodes for ofxOceanode
	ADDON_AUTHOR = @PlaymodesStudio
	ADDON_TAGS = "oceanode" "forms" "scroller"
	ADDON_URL = https://github.com/PlaymodesStudio/ofxOceanodeForms

common:
	# multiOscSender is only compiled when the project defines OFXOCEANODE_USE_OSC
	ADDON_DEPENDENCIES = ofxOceanode ofxThreadedImageLoader ofxOsc
