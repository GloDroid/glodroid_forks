.NOTPARALLEL:
default:
	-python $(LIT) -sv --param android_mode=$(LIT_MODE) \
        $(ANDROID_BUILD_TOP)/external/libcxx/test
