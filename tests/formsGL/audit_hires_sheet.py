import sys, numpy as np, subprocess, glob, os
W,H,sc=5760,3240,3
cw,ch=300,110
tiles=[]; names=[]
for f in sys.argv[2:]:
    a=np.fromfile(f,np.uint8).reshape(H,W,4).astype(np.int32)
    L=(a[:,:,:3].sum(2)*a[:,:,3])//765
    gx=np.abs(np.diff(L,axis=1)).astype(np.float64); gy=np.abs(np.diff(L,axis=0)).astype(np.float64)
    # block pattern: differences that fall on one residue (mod 3) more than on the others
    ex=[gx[:,r::3].sum() for r in range(3)]; ey=[gy[r::3,:].sum() for r in range(3)]
    mx=int(np.argmax(ex)); my=int(np.argmax(ey))
    g=np.zeros((H,W))
    wx=np.full(W-1,-0.5); wx[mx::3]=1.0
    wy=np.full(H-1,-0.5); wy[my::3]=1.0
    g[:,:-1]+=gx*wx[None,:]; g[:-1,:]+=gy*wy[:,None]
    bs=10
    gb=g[:H//bs*bs,:W//bs*bs].reshape(H//bs,bs,W//bs,bs).sum((1,3))
    from numpy.lib.stride_tricks import sliding_window_view
    sw=sliding_window_view(gb,(ch//bs,cw//bs)).sum((2,3))
    iy,ix=np.unravel_index(np.argmax(sw),sw.shape)
    y0,x0=iy*bs,ix*bs
    c=a[y0:y0+ch,x0:x0+cw]
    rgb=(c[:,:,:3]*c[:,:,3:4]//255).astype(np.uint8)
    rgb=np.repeat(np.repeat(rgb,3,0),3,1)
    tiles.append(rgb); tiles.append(np.full((6,rgb.shape[1],3),70,np.uint8)); names.append((os.path.basename(f),x0,y0))
img=np.vstack(tiles)
subprocess.run(['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','%dx%d'%(img.shape[1],img.shape[0]),'-i','-',sys.argv[1]],input=img.tobytes())
for n in names: print(n)
