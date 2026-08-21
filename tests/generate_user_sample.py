#!/usr/bin/env python3
import json
import os
from PIL import Image, PngImagePlugin

user_prompt_text = """surrealist art Surrealism, ethereal fantasy concept art of  color theme violet and red,

A whimsical soft digital painting, masterpiece, high quality,Masterpiece, intricate lines, intriguing atmosphere, sharp magnificent details, delicate features, elaborate details, (2/3 rule composition:0.5), ultra detailed, romantic,

Steampunk Theme, a windswept plateau where the ruins of an ancient temple lie, forgotten by time.,

winter,twightlight,cloudy sky,

,,

Artistic composition, intricate details.

<lora:style_of_Rembrandt_FLUX_135:0.8>,

<lora:- Flux1 - vanta_black_V2.0:0.6> vantablack,

<lora:age_v1:-1>,

<lora:113_novuschromaFLX_1:0.2> novuschroma,

<lora:softwhim:1> whimsical,

<lora:FS_v3:0.4> FS,

<lora:NixPort_style_for_semi-real_upper_body_portrait:0.3>,, Fantasy concept art style, digital painting, imaginative, dynamic composition, professional-grade execution, creates immersive world and landscape, brush strokes, epic, magical . magnificent, celestial, ethereal, painterly, epic, majestic, magical, fantasy art, cover art, dreamy, Realistic anime art style, combines anime aesthetics with lifelike details, detailed character designs, intricate backgrounds, immersive storytelling, expressive, dramatic, organic lines and forms, dreamlike and mysterious, Surrealism . dreamlike, mysterious, provocative, symbolic, intricate, detailed"""

user_negative_text = "low quality, blurry, deformed, distorted, watermark, signature"

comfy_graph = {
    "3": {
        "class_type": "KSampler",
        "inputs": {
            "cfg": 7.0,
            "denoise": 1.0,
            "latent_image": ["5", 0],
            "model": ["4", 0],
            "negative": ["7", 0],
            "positive": ["6", 0],
            "sampler_name": "euler",
            "scheduler": "normal",
            "seed": 987654321,
            "steps": 28
        }
    },
    "4": {
        "class_type": "CheckpointLoaderSimple",
        "inputs": {
            "ckpt_name": "flux1-dev.safetensors"
        }
    },
    "6": {
        "class_type": "CLIPTextEncode",
        "inputs": {
            "clip": ["4", 1],
            "text": user_prompt_text
        }
    },
    "7": {
        "class_type": "CLIPTextEncode",
        "inputs": {
            "clip": ["4", 1],
            "text": user_negative_text
        }
    }
}

img = Image.new("RGB", (64, 64), color=(60, 40, 80))
meta = PngImagePlugin.PngInfo()
meta.add_text("prompt", json.dumps(comfy_graph, indent=2))
img.save("tests/sample_user_prompt.png", pnginfo=meta)
print("Generated tests/sample_user_prompt.png with realistic ComfyUI workflow")
