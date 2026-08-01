#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
provider_dir="${repo_root}/providers/mediapipe"
python_bin="${provider_dir}/.venv/bin/python"
model_path="${provider_dir}/models/pose_landmarker_lite.task"

if [[ ! -x "${python_bin}" || ! -f "${model_path}" ]]; then
  echo "pose provider is not installed; run scripts/setup-pose-provider.sh" >&2
  exit 2
fi
if ! command -v bwrap >/dev/null 2>&1; then
  echo "pose provider requires bubblewrap (bwrap)" >&2
  exit 2
fi

# The model and interpreter can read the installed host runtime, but cannot
# write outside /tmp or open any network namespace. Model acquisition is a
# separate, explicit setup action.
exec bwrap \
  --die-with-parent \
  --new-session \
  --unshare-all \
  --ro-bind / / \
  --dev /dev \
  --proc /proc \
  --tmpfs /tmp \
  --chdir "${repo_root}" \
  --unsetenv HTTP_PROXY \
  --unsetenv HTTPS_PROXY \
  --unsetenv ALL_PROXY \
  --unsetenv http_proxy \
  --unsetenv https_proxy \
  --unsetenv all_proxy \
  --setenv MPLCONFIGDIR /tmp/matplotlib \
  --setenv TF_CPP_MIN_LOG_LEVEL 2 \
  --setenv GLOG_minloglevel 2 \
  "${python_bin}" -I "${provider_dir}/pose_provider.py" --model "${model_path}"
