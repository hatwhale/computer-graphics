// F9
// ��������
uniform sampler2D colorTexture, depthTexture;

// ������� ����������
uniform float Border;
// �������� ��������� ������
uniform float EdgeThreshold;

// ������, ��������� �������
uniform mat3 Kernel;

// ���������, ���������� �� ���������� �������
in Vertex
{
	vec2 texcoord;
} Vert;

layout(location = FRAG_OUTPUT0) out vec4 color;

// ���� �������
#define KERNEL_SIZE 9

const vec2 offset[KERNEL_SIZE] = vec2[](
	vec2(-1.0,-1.0), vec2( 0.0,-1.0), vec2( 1.0,-1.0),
	vec2(-1.0, 0.0), vec2( 0.0, 0.0), vec2( 1.0, 0.0),
	vec2(-1.0, 1.0), vec2( 0.0, 1.0), vec2( 1.0, 1.0)
);

const vec3 factor = vec3(0.27, 0.67, 0.06);

vec3 filter(in vec2 texcoord)
{
	vec2 pstep = vec2(1.0) / vec2(textureSize(colorTexture, 0));

	vec3 dx = vec3(0.0), dy = vec3(0.0);
	for (int i = 0; i < KERNEL_SIZE; ++i) {
		dx += texture(colorTexture, texcoord + offset[i] * pstep).rgb * Kernel[i/3][i%3];
		dy += texture(colorTexture, texcoord + offset[i] * pstep).rgb * Kernel[i%3][i/3];
	}
	float dist = dot(factor, sqrt(dx * dx + dy * dy));

	if(dist > EdgeThreshold)
		return 2.0 * vec3(dist);
	else
		return vec3(0.0);
}

void main()
{
    vec3 texel = Vert.texcoord.x < Border ? filter(Vert.texcoord)
		: texture(colorTexture, Vert.texcoord).rgb;

	color = vec4(texel, 1.0);
}
