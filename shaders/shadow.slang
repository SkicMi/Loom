//The shadow map pass. There is no fragment stage on purpose: the pass has no colour
//attachment, so there is nothing to return, and the depth the rasteriser writes IS the
//result. Half the work of a shadow map is work not done.
//
//Set 0 is the same FrameData every Loom pipeline gets. In a light driven pass its view and
//projection are the light's, not the camera's - that is the whole difference between this
//and the depth prepass of an ordinary frame.

struct FrameData{
    float4x4 view;
    float4x4 projection;
    float4 cameraPosition;
    float4 ambientColor;
    uint lightCount;
    uint padding0;
    uint padding1;
    uint padding2;
}

struct Push{
    float4x4 model;
    float4x4 normalMatrix;
}

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] ConstantBuffer<FrameData> frame;

//Position and nothing else. The pipeline declares the matching single attribute
//(Vertex::getPositionAttribute), while the binding stride stays sizeof(Vertex) so the same
//Mesh feeds this pass and the shaded one. Declaring the other three here would only earn a
//validation warning: Slang strips inputs the shader never reads
struct VSInput{
    [[vk::location(0)]] float3 position;
}

[shader("vertex")]
float4 vertexMain(VSInput input) : SV_Position {
    float4 world = mul(push.model, float4(input.position, 1.0));
    return mul(frame.projection, mul(frame.view, world));
}
