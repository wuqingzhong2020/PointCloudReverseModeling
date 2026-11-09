"""生成带已知尺寸、轻微噪声和背景的 T 排 PCD，供独立验收。
用法：python generate_tbar_samples.py <输出目录>；默认不覆盖已有文件。
"""
import argparse
import json
import math
import random
import struct
from pathlib import Path


def generate(kind, directory, step=3.0):
    rng = random.Random(73911)
    points = []
    angle = math.radians(20 if kind == 'inclined' else 0)
    yaw = math.radians(17)
    c,s = math.cos(yaw),math.sin(yaw)
    up = (0,math.sin(angle),math.cos(angle))
    normal = (0,math.cos(angle),-math.sin(angle))
    def add(x,y,z):
        x+=rng.gauss(0,.12);y+=rng.gauss(0,.12);z+=rng.gauss(0,.12)
        points.append((2300+c*x-s*y,-1400+s*x+c*y,800+z))
    def values(lo,hi):
        n=max(1,math.ceil((hi-lo)/step))
        return [lo+(hi-lo)*i/n for i in range(n+1)]
    def height(x):
        if kind!='irregular': return 300
        h=280+.08*(x+400)
        if 30<x<65: h-=80
        return h
    # 底板 800×180×20，立板厚 12；立板在底板上表面起立。
    for x in values(-400,400):
        for y in values(-90,90): add(x,y,20)
        for z in values(0,20):
            add(x,-90,z);add(x,90,z)
        h=height(x)
        for t in values(0,h):
            for n in [-6,6]:
                y=t*up[1]+n*normal[1];z=20+t*up[2]+n*normal[2]
                if z>=20: add(x,y,z)
        for n in values(-6,6): add(x,h*up[1]+n*normal[1],20+h*up[2]+n*normal[2])
    for x in [-400,400]:
        for y in values(-90,90):
            for z in values(0,20): add(x,y,z)
    # 异形样例置台架；另外两组置地面。背景不属于工件。
    if kind=='irregular':
        for x in [-260,260]:
            for y in range(-260,261,5):
                for dx in range(-10,11,5): add(x+dx,y,-10)
    else:
        for x in range(-550,551,8):
            for y in range(-350,351,8):
                if not (-400<x<400 and -90<y<90): add(x,y,0)
    rng.shuffle(points)
    names={'regular':'T排_规则直T_地面','irregular':'T排_异形直T_台架','inclined':'T排_规则斜T20度_地面'}
    p=directory/(names[kind]+'.pcd')
    header=('# .PCD v0.7\nVERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n'
            f'WIDTH {len(points)}\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS {len(points)}\nDATA binary\n')
    with p.open('xb') as f:
        f.write(header.encode('ascii'))
        for point in points: f.write(struct.pack('<fff',*point))
    return {'file':p.name,'pointCount':len(points),'units':'mm','type':kind,'base':[800,180,20],
            'webThickness':12,'webHeight':300 if kind!='irregular' else '280 + 0.08*(x+400); notch x=30..65 depth=80',
            'leanDegrees':math.degrees(angle),'yawDegrees':17,'originMm':[2300,-1400,800],'noiseStdMm':.12,
            'onGround':kind!='irregular','expectedWorkpieceParts':2}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args();args.directory.mkdir(parents=True,exist_ok=True)
    results=[generate(k,args.directory) for k in ['regular','irregular','inclined']]
    (args.directory/'T排样例尺寸.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(results,ensure_ascii=False,indent=2))
