import os
import re
import subprocess

import libcxx.test.config
import libcxx.android.build
import libcxx.android.test.format


class Configuration(libcxx.test.config.Configuration):
    def __init__(self, lit_config, config):
        super(Configuration, self).__init__(lit_config, config)
        self.cxx_under_test = None
        self.build_cmds_dir = None
        self.cxx_template = None
        self.link_template = None

    def configure(self):
        self.configure_src_root()
        self.configure_obj_root()

        self.configure_build_cmds()
        self.configure_cxx()
        self.configure_cxx_template()
        self.configure_link_template()
        self.configure_triple()
        self.configure_features()

    def configure_build_cmds(self):
        os.chdir(self.config.android_root)
        self.build_cmds_dir = os.path.join(self.libcxx_src_root, 'buildcmds')
        if not libcxx.android.build.mm(self.build_cmds_dir,
                                       self.config.android_root):
            raise RuntimeError('Could not generate build commands.')

    def configure_cxx(self):
        cxx_under_test_file = os.path.join(self.build_cmds_dir,
                                           'cxx_under_test')
        self.cxx_under_test = open(cxx_under_test_file).read().strip()

    def configure_cxx_template(self):
        cxx_template_file = os.path.join(self.build_cmds_dir, 'cxx.cmds')
        self.cxx_template = open(cxx_template_file).read().strip()

    def configure_link_template(self):
        link_template_file = os.path.join(self.build_cmds_dir, 'link.cmds')
        self.link_template = open(link_template_file).read().strip()

    def configure_triple(self):
        if 'clang' in self.cxx_under_test:
            triple = self.configure_clang_triple()
        else:
            triple = self.configure_gcc_triple()

        if not triple:
            raise RuntimeError('Could not determine target triple.')
        self.config.target_triple = triple

    def configure_clang_triple(self):
        match = re.search(r'-target\s+(\S+)', self.cxx_template)
        if match:
            return match.group(1)
        return None

    def configure_gcc_triple(self):
        proc = subprocess.Popen([self.cxx_under_test, '-v'],
                                stderr=subprocess.PIPE)
        _, stderr = proc.communicate()
        for line in stderr.split('\n'):
            print 'Checking {}'.format(line)
            match = re.search(r'^Target: (.+)$', line)
            if match:
                return match.group(1)
        return None

    def configure_features(self):
        self.config.available_features.add('long_tests')

    def get_test_format(self):
        mode = self.lit_config.params.get('android_mode', 'device')
        if mode == 'device':
            return libcxx.android.test.format.TestFormat(
                self.cxx_under_test,
                self.libcxx_src_root,
                self.obj_root,
                self.cxx_template,
                self.link_template,
                getattr(self.config, 'device_dir', '/data/local/tmp/'),
                getattr(self.config, 'timeout', '60'))
        elif mode == 'host':
            return libcxx.android.test.format.HostTestFormat(
                self.cxx_under_test,
                self.src_root,
                self.obj_root,
                self.cxx_template,
                self.link_template,
                getattr(self.config, 'timeout', '60'))
        else:
            raise RuntimeError('Invalid android_mode: {}'.format(mode))
