shader_type spatial;

uniform vec4 albedo : source_color = vec4(1.0);
uniform sampler2D albedo_texture : source_color, hint_default_white;
uniform bool use_vertex_color = false;
uniform bool unlit = false;
instance uniform float hlod_fade = 1.0;
instance uniform bool hlod_fade_outgoing = false;

void fragment() {
	// Complementary screen-door masks keep overlapping LOD silhouettes covered.
	ivec2 pixel = ivec2(FRAGCOORD.xy) % 8;
	float threshold = (float(pixel.x + pixel.y * 8) + 0.5) / 64.0;
	if (hlod_fade_outgoing ? threshold < 1.0 - hlod_fade : threshold >= hlod_fade) {
		discard;
	}
	vec3 color = albedo.rgb * texture(albedo_texture, UV).rgb;
	if (use_vertex_color) {
		color *= COLOR.rgb;
	}
	ALBEDO = unlit ? vec3(0.0) : color;
	EMISSION = unlit ? color : vec3(0.0);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
}
