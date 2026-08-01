#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
provider_dir="${repo_root}/providers/mediapipe"
venv_dir="${provider_dir}/.venv"
model_dir="${provider_dir}/models"
model_path="${model_dir}/pose_landmarker_lite.task"
model_url="https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task"
model_sha256="59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a"

command -v python3 >/dev/null 2>&1 || { echo "python3 is required" >&2; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "curl is required" >&2; exit 1; }
command -v bwrap >/dev/null 2>&1 || { echo "bubblewrap (bwrap) is required" >&2; exit 1; }

if [[ ! -x "${venv_dir}/bin/python" ]]; then
  python3 -m venv "${venv_dir}"
fi
"${venv_dir}/bin/python" -m pip install --disable-pip-version-check \
  -r "${provider_dir}/requirements.txt"

mkdir -p "${model_dir}"
if [[ ! -f "${model_path}" ]] || \
   [[ "$(sha256sum "${model_path}" | cut -d' ' -f1)" != "${model_sha256}" ]]; then
  partial="${model_path}.download"
  trap 'rm -f -- "${partial}"' EXIT
  curl --fail --location --proto '=https' --tlsv1.2 "${model_url}" --output "${partial}"
  actual_sha256="$(sha256sum "${partial}" | cut -d' ' -f1)"
  if [[ "${actual_sha256}" != "${model_sha256}" ]]; then
    echo "pose model checksum mismatch: expected ${model_sha256}, got ${actual_sha256}" >&2
    exit 1
  fi
  mv -- "${partial}" "${model_path}"
  trap - EXIT
fi

echo "pose provider ready: ${provider_dir}/run-provider.sh"
