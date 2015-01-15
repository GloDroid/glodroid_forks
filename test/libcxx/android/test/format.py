import os
import shlex
import time

import lit.util  # pylint: disable=import-error

import libcxx.test.format
import libcxx.android.adb as adb


class HostTestFormat(libcxx.test.format.LibcxxTestFormat):
    # pylint: disable=super-init-not-called
    def __init__(self, cxx_under_test, libcxx_src_root, libcxx_obj_root,
                 cxx_template, link_template, timeout):
        self.cxx_under_test = cxx_under_test
        self.libcxx_src_root = libcxx_src_root
        self.libcxx_obj_root = libcxx_obj_root
        self.cxx_template = cxx_template
        self.link_template = link_template
        self.timeout = timeout
        self.use_verify_for_fail = False

    def _compile(self, output_path, source_path, use_verify=False):
        if use_verify:
            raise NotImplementedError(
                'AndroidConfiguration does not support use_verify mode.')
        cxx_args = self.cxx_template.replace('%OUT%', output_path)
        cxx_args = cxx_args.replace('%SOURCE%', source_path)
        cmd = [self.cxx_under_test] + shlex.split(cxx_args)
        out, err, exit_code = lit.util.executeCommand(cmd)
        return cmd, out, err, exit_code

    def _link(self, exec_path, object_path):
        link_args = self.link_template.replace('%OUT%', exec_path)
        link_args = link_args.replace('%SOURCE%', object_path)
        cmd = [self.cxx_under_test] + shlex.split(link_args)
        out, err, exit_code = lit.util.executeCommand(cmd)
        return cmd, out, err, exit_code

    def _run(self, exec_path, lit_config, in_dir=None):
        cmd = [exec_path]
        # We need to use LD_LIBRARY_PATH because the build system's rpath is
        # relative, which won't work since we're running from /tmp. We can
        # either scan `cxx_under_test`/`link_template` to determine whether
        # we're 32-bit or 64-bit, scan testconfig.mk, or just add both
        # directories and let the linker sort it out. I'm choosing the lazy
        # option.
        outdir = os.getenv('ANDROID_HOST_OUT')
        libpath = os.pathsep.join([
            os.path.join(outdir, 'lib'),
            os.path.join(outdir, 'lib64'),
        ])
        out, err, rc = lit.util.executeCommand(
            cmd, cwd=in_dir, env={'LD_LIBRARY_PATH': libpath})
        return self._make_report(cmd, out, err, rc)


class TestFormat(HostTestFormat):
    def __init__(self, cxx_under_test, libcxx_src_root, libcxx_obj_root,
                 cxx_template, link_template, device_dir, timeout):
        HostTestFormat.__init__(
            self,
            cxx_under_test,
            libcxx_src_root,
            libcxx_obj_root,
            cxx_template,
            link_template,
            timeout)
        self.device_dir = device_dir

    def _working_directory(self, file_name):
        return os.path.join(self.device_dir, file_name)

    def _wd_path(self, test_name, file_name):
        return os.path.join(self._working_directory(test_name), file_name)

    def _build(self, exec_path, source_path, compile_only=False,
               use_verify=False):
        # pylint: disable=protected-access
        cmd, report, rc = libcxx.test.format.LibcxxTestFormat._build(
            self, exec_path, source_path, compile_only, use_verify)
        if rc != 0:
            return cmd, report, rc

        try:
            exec_file = os.path.basename(exec_path)

            adb.mkdir(self._working_directory(exec_file))
            adb.push(exec_path, self._wd_path(exec_file, exec_file))

            # Push any .dat files in the same directory as the source to the
            # working directory.
            src_dir = os.path.dirname(source_path)
            data_files = [f for f in os.listdir(src_dir) if f.endswith('.dat')]
            for data_file in data_files:
                df_path = os.path.join(src_dir, data_file)
                df_dev_path = self._wd_path(exec_file, data_file)
                adb.push(df_path, df_dev_path)
            return cmd, report, rc
        except adb.AdbError as ex:
            return self._make_report(ex.cmd, ex.out, ex.err, ex.exit_code)

    def _clean(self, exec_path):
        exec_file = os.path.basename(exec_path)
        cmd = ['adb', 'shell', 'rm', '-rf', self._working_directory(exec_file)]
        lit.util.executeCommand(cmd)
        try:
            os.remove(exec_path)
        except OSError:
            pass

    def _run(self, exec_path, lit_config, in_dir=None):
        exec_file = os.path.basename(exec_path)
        shell_cmd = 'cd {} && {}; echo $?'.format(
            self._working_directory(exec_file),
            self._wd_path(exec_file, exec_file))
        cmd = ['timeout', self.timeout, 'adb', 'shell', shell_cmd]

        # Tests will commonly fail with ETXTBSY. Possibly related to this bug:
        # https://code.google.com/p/android/issues/detail?id=65857. Work around
        # it by just waiting a second and then retrying.
        for _ in range(10):
            out, err, exit_code = lit.util.executeCommand(cmd)
            if exit_code == 0:
                if 'Text file busy' in out:
                    time.sleep(1)
                else:
                    out = out.strip().split('\r\n')
                    status_line = out[-1:][0]
                    out = '\n'.join(out[:-1])
                    exit_code = int(status_line)
                    break
            else:
                err += '\nTimed out after {} seconds'.format(self.timeout)
                break
        return self._make_report(cmd, out, err, exit_code)
