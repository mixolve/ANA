// Radiant / Paul Bakaus (MIT), ported from AVA. See resources/licenses.md.
// Adaptations: JUCE coordinates/alpha, requested SCALE, #222222 film base, and
// a post-render colour filter and a movable strip crop. The original
// noise and burn layers run continuously at the developed burn stage, without fades.
#pragma once
inline constexpr auto burningFilmFragmentShader = R"burnshader(
uniform vec2 u_origin;
uniform float u_coordinateScale;
uniform float u_cropOffset;
uniform float u_patternScale;
uniform vec3 u_colourRow0;
uniform vec3 u_colourRow1;
uniform vec3 u_colourRow2;
uniform float u_time;
uniform vec2 u_res;
uniform float u_burnSpeed;
uniform float u_emberGlow;

float hash21(vec2 p) {
  p = fract(p * vec2(443.897, 441.423));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}

vec2 hash22(vec2 p) {
  vec3 a = fract(p.xyx * vec3(443.897, 441.423, 437.195));
  a += dot(a, a.yzx + 19.19);
  return fract((a.xx + a.yz) * a.zy);
}

float noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  vec2 shift = vec2(100.0);
  mat2 rot = mat2(0.866, 0.5, -0.5, 0.866);
  for (int i = 0; i < 6; i++) {
    v += a * noise(p);
    p = rot * p * 2.0 + shift;
    a *= 0.5;
  }
  return v;
}

float warpedNoise(vec2 p, float t) {
  vec2 q = vec2(
    fbm(p + vec2(0.0, 0.0)),
    fbm(p + vec2(5.2, 1.3))
  );

  vec2 r = vec2(
    fbm(p + 4.0 * q + vec2(1.7, 9.2) + 0.05 * t),
    fbm(p + 4.0 * q + vec2(8.3, 2.8) + 0.06 * t)
  );

  return fbm(p + 4.0 * r);
}

float emberNoise(vec2 p, float t) {
  float n1 = noise(p * 3.0 + vec2(t * 0.3, t * 0.2));
  float n2 = noise(p * 7.0 - vec2(t * 0.5, t * 0.15));
  float n3 = noise(p * 15.0 + vec2(t * 0.8, -t * 0.4));
  return n1 * 0.5 + n2 * 0.35 + n3 * 0.15;
}

float filmGrain(vec2 uv, float t) {
  float grain = hash21(uv * u_res * 0.5 + fract(t * 137.0));
  return grain;
}

void main() {
  vec2 canvasPosition = (pixelPos - u_origin) * u_coordinateScale + vec2(0.0, u_cropOffset);
  vec2 uv = vec2(canvasPosition.x, u_res.y - canvasPosition.y) / u_res;
  float t = u_time;

  // Keep the original burn frontier in its developed stage indefinitely.
  float cycleT = t;
  float burnThreshold = 0.50;

  // Use aspect-corrected coordinates for the noise
  float aspect = u_res.x / u_res.y;
  vec2 noiseUV = uv * vec2(aspect, 1.0) * u_patternScale;

  // The original domain warp evolves continuously, with no cycle jump/reset.
  vec2 cycleOffset = vec2(0.0);

  float burnNoise = warpedNoise(noiseUV + cycleOffset, cycleT * u_burnSpeed * 0.3);

  float burnAmount = smoothstep(burnThreshold, burnThreshold - 0.12, burnNoise);

  float edgeWidth = 0.06;
  float edgeInner = smoothstep(burnThreshold, burnThreshold - edgeWidth, burnNoise);
  float edgeMask = edgeInner * (1.0 - burnAmount * 0.7);

  float hotEdge = smoothstep(burnThreshold + 0.01, burnThreshold - 0.01, burnNoise)
                - smoothstep(burnThreshold - 0.01, burnThreshold - 0.04, burnNoise);
  hotEdge = max(hotEdge, 0.0);

  float grain = filmGrain(uv, t);
  float grainStrength = 0.035;
  vec3 filmBase = vec3(34.0 / 255.0);
  filmBase += (grain - 0.5) * grainStrength;

  float scanline = sin(uv.y * u_res.y * 0.5) * 0.5 + 0.5;
  filmBase *= 0.95 + scanline * 0.05;

  vec3 whiteHot = vec3(1.0, 0.88, 0.67);    // #ffe0aa
  vec3 orangeGlow = vec3(1.0, 0.533, 0.2);   // #ff8833
  vec3 amberEdge = vec3(0.784, 0.584, 0.424); // #c8956c
  vec3 deepAmber = vec3(0.5, 0.25, 0.08);

  vec3 edgeColor = mix(amberEdge, orangeGlow, smoothstep(0.0, 0.5, edgeMask));
  edgeColor = mix(edgeColor, whiteHot, hotEdge);

  float edgeNoise = noise(noiseUV * 8.0 + cycleOffset + t * 0.5);
  edgeColor *= 0.8 + edgeNoise * 0.4;

  float edgePulse = 0.85 + 0.15 * sin(t * 3.0 + burnNoise * 10.0);
  edgeColor *= edgePulse;

  float ember = emberNoise(noiseUV, t);

  vec3 emberDark = vec3(0.1, 0.02, 0.0);
  vec3 emberRed = vec3(0.4, 0.04, 0.0);       // #660a00
  vec3 emberOrange = vec3(0.85, 0.35, 0.05);
  vec3 emberWhite = vec3(1.0, 0.7, 0.3);

  vec3 emberColor = mix(emberDark, emberRed, smoothstep(0.2, 0.5, ember));
  emberColor = mix(emberColor, emberOrange, smoothstep(0.55, 0.75, ember));
  emberColor = mix(emberColor, emberWhite, smoothstep(0.8, 0.95, ember) * 0.5);

  float emberPulse = 0.7 + 0.3 * sin(t * 2.0 + ember * 8.0 + burnNoise * 5.0);
  emberColor *= emberPulse * u_emberGlow;

  float edgeProximity = smoothstep(0.3, 0.0, abs(burnNoise - burnThreshold + 0.15));
  emberColor += orangeGlow * edgeProximity * 0.4 * u_emberGlow;

  vec3 col = filmBase;

  float preheat = smoothstep(burnThreshold + 0.15, burnThreshold + 0.02, burnNoise);
  preheat *= (1.0 - burnAmount);
  col += deepAmber * preheat * 0.4;

  col = mix(col, emberColor, burnAmount);

  col += edgeColor * edgeMask * 1.8;

  col += whiteHot * hotEdge * 1.2;

  float curl = noise(noiseUV * 20.0 + cycleOffset);
  float curlEdge = smoothstep(burnThreshold + 0.03, burnThreshold - 0.02, burnNoise);
  curlEdge *= (1.0 - burnAmount * 0.8);
  col += vec3(0.6, 0.3, 0.1) * curl * curlEdge * 0.3;

  for (float i = 0.0; i < 4.0; i++) {
    vec2 sparkUV = uv * vec2(aspect, 1.0);
    float sparkScale = 30.0 + i * 15.0;
    vec2 sparkPos = sparkUV * sparkScale;
    sparkPos.y -= t * (1.5 + i * 0.8);
    sparkPos.x += sin(t * (1.0 + i * 0.3) + i * 3.0) * 0.5;

    vec2 sparkId = floor(sparkPos);
    vec2 sparkFrac = fract(sparkPos) - 0.5;

    float sparkHash = hash21(sparkId + i * 100.0);
    float sparkActive = step(0.85, sparkHash);

    vec2 sparkOffset = hash22(sparkId + i * 50.0) - 0.5;
    float sparkDist = length(sparkFrac - sparkOffset * 0.3);
    float sparkSize = 0.03 + sparkHash * 0.02;
    float spark = smoothstep(sparkSize, sparkSize * 0.2, sparkDist);

    float sparkFlicker = sin(t * 15.0 + sparkHash * 50.0) * 0.5 + 0.5;
    spark *= sparkFlicker * sparkActive;

    float nearBurn = smoothstep(0.6, 0.3, abs(burnNoise - burnThreshold));
    spark *= nearBurn;

    vec3 sparkColor = mix(orangeGlow, whiteHot, sparkHash);
    col += sparkColor * spark * 0.6 * u_emberGlow;
  }

  vec2 vc = uv - 0.5;
  float vig = 1.0 - dot(vc, vc) * 1.6;
  vig = clamp(vig, 0.0, 1.0);
  vig = pow(vig, 0.6);
  col *= vig;

  col = mix(col, col * vec3(1.05, 0.95, 0.85), 0.15);

  // Tone mapping keeps ember highlights from clipping.
  col = col / (1.0 + col * 0.2);

  col = pow(max(col, 0.0), vec3(0.95));

  // Presentation filter; the original burn layers above are unchanged.
  col = clamp(col, 0.0, 1.0);
  col = vec3(dot(col, u_colourRow0), dot(col, u_colourRow1), dot(col, u_colourRow2));
  gl_FragColor = vec4(col * pixelAlpha, pixelAlpha);
}
)burnshader";
