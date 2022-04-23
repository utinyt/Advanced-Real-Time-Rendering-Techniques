..\..\glslc.exe geometry.vert -o geometry_vert.spv -g
..\..\glslc.exe geometry.frag -o geometry_frag.spv -g
..\..\glslc.exe skydome.vert -o skydome_vert.spv -g
..\..\glslc.exe skydome.frag -o skydome_frag.spv -g
..\..\glslc.exe full_quad.vert -o full_quad_vert.spv -g
..\..\glslc.exe lighting_pass.frag -o lighting_pass_frag.spv -g
..\..\glslc.exe postprocess.frag -o postprocess_frag.spv -g
..\..\glslc.exe ao.frag -o ao_frag.spv -g
..\..\glslc.exe local_lights.vert -o local_lights_vert.spv -g
..\..\glslc.exe local_lights.frag -o local_lights_frag.spv -g
..\..\glslc.exe shadow_map.vert -o shadow_map_vert.spv -g
..\..\glslc.exe shadow_map.frag -o shadow_map_frag.spv -g
..\..\glslc.exe shadow_map_horizontal_blur.comp -o shadow_map_horizontal_blur_comp.spv -g
..\..\glslc.exe shadow_map_vertical_blur.comp -o shadow_map_vertical_blur_comp.spv -g
..\..\glslc.exe ao_horizontal_blur.comp -o ao_horizontal_blur_comp.spv -g
..\..\glslc.exe ao_vertical_blur.comp -o ao_vertical_blur_comp.spv -g
pause