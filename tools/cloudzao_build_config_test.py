#!/usr/bin/env python3
"""离线验证 CloudZao 构建配置隔离。/ Verify CloudZao build configuration isolation offline."""
import os
import subprocess
import tempfile
from pathlib import Path


CONFIG = Path(__file__).resolve().parents[1] / "main/services/cloudzao_build_config.cmake"


def main():
    with tempfile.TemporaryDirectory(prefix="mogo-cloudzao-config-") as tmp:
        root = Path(tmp)
        home = root / "home"
        local = home / ".config/mogo-e4p/cloudzao-integration-id"
        local.parent.mkdir(parents=True)
        (root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(cloudzao_policy NONE)
option(MOGO_PUBLIC_RELEASE "Release mode" OFF)
option(CONFIG_MOGO_CLOUDZAO_TRANSFER "CloudZao support" ON)
include("{CONFIG.as_posix()}")
mogo_cloudzao_integration_id(id)
file(CONFIGURE OUTPUT "chosen-id" CONTENT "@id@" @ONLY)
''')
        env = dict(os.environ, HOME=str(home), MOGO_CLOUDZAO_INTEGRATION_ID="synthetic-release")
        env.pop("CI", None)

        def cmake(*args, run_env=env, error=None):
            result = subprocess.run(["cmake", *args], env=run_env, cwd=root,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                    timeout=5)
            if error is None:
                assert result.returncode == 0, result.stdout
            else:
                assert result.returncode != 0 and error in result.stdout, result.stdout
                assert "invalid!fixture" not in result.stdout, result.stdout

        def configure(tree, *args, **kwargs):
            cmake("-S", ".", "-B", tree, "-G", "Ninja", *args, **kwargs)

        def chosen(tree, expected):
            assert (root / tree / "chosen-id").read_text() == expected
            if expected:
                assert expected not in (root / tree / "CMakeCache.txt").read_text()

        # 不因普通环境变量或已存在的缓存意外启用；文件变动触发增量重新配置。
        # Ordinary environment variables do not opt in; file changes trigger reconfiguration.
        configure("local")
        chosen("local", "")
        local.write_text("synthetic-local\n")
        cmake("--build", "local")
        chosen("local", "synthetic-local")
        local.write_text("synthetic-rotated\n")
        cmake("--build", "local")
        chosen("local", "synthetic-rotated")
        local.unlink()
        cmake("--build", "local")
        chosen("local", "")

        local.write_text("invalid!fixture\n")
        cmake("--build", "local", error="Missing or invalid integration ID")
        configure("ci", run_env=dict(env, CI="true"))
        chosen("ci", "")
        local.write_text("synthetic-local\n")
        configure("disabled", "-DCONFIG_MOGO_CLOUDZAO_TRANSFER=OFF",
                  error="Local CloudZao is configured")
        configure("release", "-DMOGO_PUBLIC_RELEASE=ON", run_env=dict(env, CI="true"))
        chosen("release", "synthetic-release")
        configure("release", "-DMOGO_PUBLIC_RELEASE=ON",
                  run_env=dict(env, CI="true", MOGO_CLOUDZAO_INTEGRATION_ID=""),
                  error="Missing or invalid integration ID")
    print("CloudZao build configuration: PASS (local add/edit/remove, CI isolation, release secret only)")


if __name__ == "__main__":
    main()
