$S = "C:\Users\Metaverse\Documents\PCB_Essen\Tools\butchery\render_tour.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File $S -Level "/Game/level5_cutaway" -Name "Butchery_Tour_60fps" -Width 2560 -Height 1440 -TimeoutMinutes 180
powershell -NoProfile -ExecutionPolicy Bypass -File $S -Level "/Game/level5" -Name "Butchery_Exterior_60fps" -Width 2560 -Height 1440 -TimeoutMinutes 60
