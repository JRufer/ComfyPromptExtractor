import json
from PIL import Image, PngImagePlugin

# 1. Standard InvokeAI sd-metadata PNG
img1 = Image.new("RGB", (64, 64), color=(30, 40, 50))
meta1 = PngImagePlugin.PngInfo()
meta1.add_text("sd-metadata", json.dumps({
    "positive_prompt": "a colorful cyberpunk artist holding a spray gun, eclipse in sky, detailed artwork",
    "negative_prompt": "ugly, blurry, low quality",
    "model": "sdxl"
}))
img1.save("tests/sample_invokeai.png", pnginfo=meta1)
print("Generated tests/sample_invokeai.png")

# 2. InvokeAI Core Metadata Graph PNG
img2 = Image.new("RGB", (64, 64), color=(40, 50, 60))
meta2 = PngImagePlugin.PngInfo()
meta2.add_text("invokeai_metadata", json.dumps({
    "core_metadata": {
        "positive_prompt": "cyberpunk girl with goggles on rooftop",
        "negative_prompt": "distorted hands, bad anatomy"
    }
}))
img2.save("tests/sample_invokeai_graph.png", pnginfo=meta2)
print("Generated tests/sample_invokeai_graph.png")

# 3. Legacy InvokeAI Dream CLI PNG
img3 = Image.new("RGB", (64, 64), color=(50, 60, 70))
meta3 = PngImagePlugin.PngInfo()
meta3.add_text("Dream", "steampunk temple ruins in winter -s 50 -W 512 -H 512 -n \"lowres, bad quality\"")
img3.save("tests/sample_invokeai_dream.png", pnginfo=meta3)
print("Generated tests/sample_invokeai_dream.png")
