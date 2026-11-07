# Dependencies, provenance and license verification

Checked during implementation on 2026-10-29. No third-party model or binary is redistributed in the source ZIP.

| Component | Pinned version | Upstream / license |
|---|---|---|
| MediaPipe Python | 0.10.21 | [PyPI release metadata](https://pypi.org/pypi/mediapipe/0.10.21/json), [source](https://github.com/google-ai-edge/mediapipe). PyPI declares Apache 2.0; a `cp311-cp311-win_amd64` wheel was confirmed. |
| OpenCV contrib Python | 4.11.0.86 | [Package](https://pypi.org/project/opencv-contrib-python/4.11.0.86/), [packaging licenses](https://github.com/opencv/opencv-python). OpenCV uses Apache 2.0; wheel packaging and bundled components carry their own notices. |
| NumPy | 1.26.4 | [Release](https://pypi.org/project/numpy/1.26.4/), [license](https://numpy.org/doc/1.26/license.html): BSD-3-Clause and bundled component notices. |

Only direct dependencies are pinned here; pip resolves transitive dependencies. Keep the environment feature-local, run `pip check`, and retain wheel/dist-info license notices if distributing a runtime separately. This source transfer is not an offline dependency bundle or a full transitive lockfile.

## Pose model

Official task guide: <https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker/python>

Official model overview: <https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker/index>

Official BlazePose GHUM 3D model card: <https://storage.googleapis.com/mediapipe-assets/Model%20Card%20BlazePose%20GHUM%203D.pdf>

Pinned Lite float16 version 1 (not `latest`):

<https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task>

SHA256:

```text
59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a
```

The official download and checksum were verified. The bundle contains `pose_detector.tflite` and `pose_landmarks_detector.tflite`. No explicit model license text was found in the archive, embedded readable metadata, or download headers. The documentation's CC-BY text / Apache code-sample notice is **not proof of the weights' license**. Attempts to verify a model-specific Kaggle listing returned 404.

**Model-weight license verification remains unresolved.** Do not label these weights Apache-2.0 merely because the MediaPipe software is Apache-2.0. Review the official model card and obtain clarification from the model publisher before commercial use or model redistribution. The source ZIP deliberately excludes the weights. `Setup.ps1` provides the official download as a separate user-run preparation step, not a license grant.

한국어 요약: 소프트웨어 패키지 라이선스와 모델 가중치 라이선스는 별개입니다. 공식 모델 출처/버전/해시는 확인했지만 가중치의 명시적 이용 조건은 이번 환경에서 확인되지 않았습니다. 상업 이용/재배포 전에 Google의 모델별 조건을 별도로 확인하세요. 모델은 소스 ZIP에 넣지 않았습니다.

## Runtime behavior

The worker uses Tasks `VIDEO` mode with strictly increasing capture timestamps. Synchronous model inference runs **only in a separate Python process**, not on the native UI thread. A camera read thread retains only the latest image, so slow inference skips intermediate capture frames rather than accumulating images. No segmentation output, image persistence, telemetry, application network code, LLM or Copilot Studio integration is used by this feature.

World landmarks are monocular estimates, not absolute position/motion-capture measurements. The implementation is intended for animation prototyping, not biometric identification, diagnosis, safety decisions or precise measurement.
