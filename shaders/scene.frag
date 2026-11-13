#version 450
layout(location=0) in vec2 out_uv;
layout(location=1) in vec4 out_color;
layout(location=2) in float out_iw;
layout(location=3) in vec3 out_world;
layout(location=4) in vec3 out_normal;
layout(location=5) in vec3 out_sun;
layout(location=6) flat in vec4 out_texinfo;
layout(location=7) flat in vec2 out_alpha;
layout(location=0) out vec4 output_color;
#ifdef EMBER_GL
uniform usamplerBuffer resources;
layout(std140) uniform Settings {
#else
layout(set=0,binding=0) uniform usamplerBuffer resources;
layout(push_constant) uniform Settings {
#endif
 int profile;int crt;int height;int shadow_size;
 vec4 right;vec4 up;vec4 forward;vec4 center;
 int shadow_offset;float grid;float bias;float pad;
} settings;
vec3 checker_texel(ivec2 p){int c=((p.x/2)&1)^((p.y/2)&1);return vec3(c!=0?0.68:1.0);}
vec3 sample_checker(vec2 p){p*=8.0;if(settings.profile!=2)return checker_texel(ivec2(floor(p)));p-=0.5;ivec2 i=ivec2(floor(p));vec2 f=fract(p);if(f.x+f.y<=1)return checker_texel(i)*(1-f.x-f.y)+checker_texel(i+ivec2(1,0))*f.x+checker_texel(i+ivec2(0,1))*f.y;return checker_texel(i+1)*(f.x+f.y-1)+checker_texel(i+ivec2(0,1))*(1-f.x)+checker_texel(i+ivec2(1,0))*(1-f.y);}
int coord(int x,int n,int w){if(w==1)return clamp(x,0,n-1);if(w==2){int a=x%(2*n);if(a<0)a+=2*n;return a<n?a:2*n-a-1;}x=x%n;return x<0?x+n:x;}
vec4 asset_texel(ivec2 p){int w=int(out_texinfo.y),h=int(out_texinfo.z),wrap=int(out_texinfo.w);int i=int(out_texinfo.x)+coord(p.y,h,wrap/4)*w+coord(p.x,w,wrap%4);uint c=texelFetch(resources,i).r;return vec4(float(c&255u),float((c>>8)&255u),float((c>>16)&255u),float(c>>24))/255.0;}
vec4 sample_asset(vec2 p){p*=out_texinfo.yz;if(settings.profile!=2)return asset_texel(ivec2(floor(p)));p-=.5;ivec2 i=ivec2(floor(p));vec2 f=fract(p);if(f.x+f.y<=1)return asset_texel(i)*(1-f.x-f.y)+asset_texel(i+ivec2(1,0))*f.x+asset_texel(i+ivec2(0,1))*f.y;return asset_texel(i+1)*(f.x+f.y-1)+asset_texel(i+ivec2(0,1))*(1-f.x)+asset_texel(i+ivec2(1,0))*(1-f.y);}
float shadow(vec3 p,vec3 n){if(settings.shadow_size==0)return 1.0;
 if(settings.grid>0){vec3 q=(floor(p/settings.grid)+.5)*settings.grid;n=normalize(n);p=q-n*dot(q-p,n);}
 p-=settings.center.xyz;vec3 lp=vec3(dot(p,settings.right.xyz),dot(p,settings.up.xyz),dot(p,settings.forward.xyz));vec2 pixel=(lp.xy/settings.right.w+.5)*settings.shadow_size;if(!all(greaterThanEqual(pixel,vec2(0)))||!all(lessThan(pixel,vec2(settings.shadow_size))))return 1.0;ivec2 i=ivec2(pixel);float depth=uintBitsToFloat(texelFetch(resources,settings.shadow_offset+i.y*settings.shadow_size+i.x).r);n=normalize(n);float slope=(abs(dot(n,settings.right.xyz))+abs(dot(n,settings.up.xyz)))/max(.1,abs(dot(n,settings.forward.xyz)));float bias=settings.bias+.6*(settings.right.w/settings.shadow_size)*slope;return lp.z<=depth+bias?1.0:.22;
}
void main(){vec3 sun=out_sun/out_iw;float vis=1;if(dot(sun,vec3(1))>0)vis=shadow(out_world/out_iw,out_normal/out_iw);vec3 c=out_color.rgb/out_iw+sun*vis;
 float alpha=out_alpha.y;if(out_texinfo.y>0){vec4 t=sample_asset(out_uv/out_iw);c*=t.rgb;alpha*=t.a;}else if(out_color.a>.5)c*=sample_checker(out_uv/out_iw);if(alpha<out_alpha.x)discard;
#ifdef EMBER_GL
 ivec2 p=ivec2(gl_FragCoord.x,float(settings.height)-gl_FragCoord.y);
#else
 ivec2 p=ivec2(gl_FragCoord.xy);
#endif
 if(settings.profile==1){int b[16]=int[](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);float d=(float(b[(p.y&3)*4+(p.x&3)])/16.0-0.5)/31.0;c=floor(clamp(c+d,0,1)*31+0.5)/31;}
 if(settings.crt!=0){c*=((p.y&1)!=0)?0.72:1.0;vec3 m=vec3(0.86);m[p.x%3]=1;c*=m;}output_color=vec4(clamp(c,0,1),1);
}
