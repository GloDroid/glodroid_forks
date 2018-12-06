import os
import re

import libcxx.test.config
import libcxx.android.compiler
import libcxx.android.test.format


class Configuration(libcxx.test.config.Configuration):
    def __init__(self, lit_config, config):
        super(Configuration, self).__init__(lit_config, config)

    def configure(self):
        self.configure_src_root()
        self.configure_obj_root()

        self.configure_cxx()
        self.configure_triple()
        self.configure_features()

    def print_config_info(self):
        self.lit_config.note(
            'Using compiler: {}'.format(self.cxx.path))
        self.lit_config.note(
            'Using compile template: {}'.format(self.cxx.cxx_template))
        self.lit_config.note(
            'Using link template: {}'.format(self.cxx.link_template))
        self.lit_config.note('Using available_features: %s' %
                             list(self.config.available_features))

    def configure_obj_root(self):
        if self.lit_config.params.get('android_mode') == 'host':
            self.libcxx_obj_root = os.getenv('ANDROID_HOST_OUT')
        else:
            self.libcxx_obj_root = os.getenv('ANDROID_PRODUCT_OUT')

    def configure_cxx(self):
        cxx_under_test = self.lit_config.params.get('cxx_under_test')
        cxx_template = self.lit_config.params.get('cxx_template')
        link_template = self.lit_config.params.get('link_template')

        self.cxx = libcxx.android.compiler.AndroidCXXCompiler(
            cxx_under_test, cxx_template, link_template)

    def configure_triple(self):
        # The libcxxabi test suite needs this but it doesn't actually
        # use it for anything important.
        self.config.host_triple = ''

        self.config.target_triple = self.cxx.get_triple()

    def configure_features(self):
        self.config.available_features.add('long_tests')
        std_pattern = re.compile(r'-std=(c\+\+\d[0-9x-z])')
        match = std_pattern.search(self.cxx.cxx_template)
        if match:
            self.config.available_features.add(match.group(1))

    def get_test_format(self):
        mode = self.lit_config.params.get('android_mode', 'device')
        if mode == 'device':
            return libcxx.android.test.format.TestFormat(
                self.cxx,
                self.libcxx_src_root,
                self.libcxx_obj_root,
                getattr(self.config, 'device_dir', '/data/local/tmp/'),
                getattr(self.config, 'timeout', '60'))
        elif mode == 'host':
            return libcxx.android.test.format.HostTestFormat(
                self.cxx,
                self.libcxx_src_root,
                self.libcxx_obj_root,
                getattr(self.config, 'timeout', '60'))
        else:
            raise RuntimeError('Invalid android_mode: {}'.format(mode))
