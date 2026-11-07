"""Rebuild the small, portable schema-1 fixture; Python standard library only."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def transform(position=(0, 0, 0)):
    return dict(translation=list(position), rotation=[0, 0, 0, 1], scale=[1, 1, 1])


def mesh(name, positions, bone):
    inverse = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -bone, 0, 1]
    return dict(name=name, material=bone, indices=[0, 2, 1, 0, 3, 2],
                skin=[dict(bone=bone, inverseBind=inverse)],
                vertices=[dict(p=list(p), n=[0, 0, -1], uv=uv, j=[0, 0, 0, 0], w=[1, 0, 0, 0])
                          for p, uv in zip(positions, [[0, 1], [1, 1], [1, 0], [0, 0]])])


def clip(ident, name, speed, tracks):
    return dict(format="lunaflow.clip", version=1, id=ident, name=name, duration=2, speed=speed,
                loop=True, revision=1, coordinates=dict(handedness="left", up="+Y", unit="meter",
                quaternion="xyzw", matrix="row-vector; local = S*R*T; global = local*parent"), tracks=tracks)


def main():
    materials = [dict(name="Corner checker", baseColor=[1, 1, 1, 1], texturePath="", textureBytes=[],
                      textureRGBA=[255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255],
                      textureWidth=2, textureHeight=2),
                 dict(name="Purple arm", baseColor=[.6, .3, 1, 1], texturePath="", textureBytes=[],
                      textureRGBA=[], textureWidth=0, textureHeight=0)]
    track = dict(bonePath="Root/Arm", positions=[], scales=[], rotations=[
        dict(time=0, value=[0, 0, 0, 1]),
        dict(time=1, value=[0, 0, 2 ** -.5, 2 ** -.5]),
        dict(time=2, value=[0, 0, 0, 1])])
    bones = [dict(name="Root", path="Root", parent=-1, bindLocal=transform()),
             dict(name="Arm", path="Root/Arm", parent=0, bindLocal=transform((0, 1, 0))),
             dict(name="Hand", path="Root/Arm/Hand", parent=1, bindLocal=transform((.5, 0, 0)))]
    particles = dict(shape=1, duration=1, rate=20, lifetime=.4, lifetimeRandomness=0, speed=.15,
                     speedRandomness=0, gravity=0, radius=.02, angle=20, startSize=.08, endSize=0,
                     box=[1, 1, 1], velocity=[0, .1, 0], endColor=[.1, .7, 1, 0], seed=1,
                     maxParticles=64, burst=4, loop=True, worldSpace=False, additive=True)
    effect = dict(id="hand-magic", name="Hand Magic", clipId="wave", type=3, bone=2, local=transform(),
                  color=[.2, .7, 1, 1], size=1, intensity=1, start=0, end=1, enabled=True,
                  sourcePrompt="", particles=particles)
    project = dict(format="lunaflow.project", schema_version=1, model_path="",
                   scene=dict(bones=bones, materials=materials, meshes=[
                       mesh("Body", [(-.2, 0, 0), (.2, 0, 0), (.2, 1, 0), (-.2, 1, 0)], 0),
                       mesh("Arm mesh", [(0, .9, 0), (.6, .9, 0), (.6, 1.1, 0), (0, 1.1, 0)], 1)],
                       objects=[dict(id="sample", name="Sample", root=0)], effects=[effect]),
                   clips=[clip("wave", "Wave (2x)", 2, [track]), clip("idle", "Idle", 1, [])],
                   editor=dict(clipId="wave", bonePath="Root/Arm", time=0, cameraTarget=[0, .8, 0],
                               cameraYaw=0, cameraPitch=0, cameraDistance=3))
    path = ROOT / "Samples~" / "AnimatedSample" / "Wave.lunaflow"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(project, indent=2) + "\n", encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
