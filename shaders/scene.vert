#version 450
layout(location=0) in vec4 position_iw;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color_checker;
layout(location=3) in vec3 world;
layout(location=4) in vec3 normal;
layout(location=5) in vec3 sun;
layout(location=6) in vec4 texinfo;
layout(location=7) in vec2 alpha;
layout(location=0) out vec2 out_uv;
layout(location=1) out vec4 out_color;
layout(location=2) out float out_iw;
layout(location=3) out vec3 out_world;
layout(location=4) out vec3 out_normal;
layout(location=5) out vec3 out_sun;
layout(location=6) flat out vec4 out_texinfo;
layout(location=7) flat out vec2 out_alpha;
void main(){
#ifdef EMBER_GL
 gl_Position=vec4(position_iw.x,-position_iw.y,position_iw.z*2.0-1.0,1);
#else
 gl_Position=vec4(position_iw.xyz,1);
#endif
 out_uv=uv;out_color=color_checker;out_iw=position_iw.w;out_world=world;out_normal=normal;out_sun=sun;out_texinfo=texinfo;out_alpha=alpha;
}
