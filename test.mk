.NOTPARALLEL:
default:
	cp $(ANDROID_BUILD_TOP)/external/libcxx/test/lit.$(LIT_MODE).cfg \
	   $(ANDROID_BUILD_TOP)/external/libcxx/test/lit.site.cfg
	-python $(LIT) -sv $(ANDROID_BUILD_TOP)/external/libcxx/test
	rm $(ANDROID_BUILD_TOP)/external/libcxx/test/lit.site.cfg
