package com.zying.zysu.ui.theme

import androidx.compose.ui.graphics.Color

sealed class ThemeColors {
    // Light theme
    abstract val primaryLight: Color
    abstract val onPrimaryLight: Color
    abstract val primaryContainerLight: Color
    abstract val onPrimaryContainerLight: Color
    abstract val secondaryLight: Color
    abstract val onSecondaryLight: Color
    abstract val secondaryContainerLight: Color
    abstract val onSecondaryContainerLight: Color
    abstract val tertiaryLight: Color
    abstract val onTertiaryLight: Color
    abstract val tertiaryContainerLight: Color
    abstract val onTertiaryContainerLight: Color
    abstract val errorLight: Color
    abstract val onErrorLight: Color
    abstract val errorContainerLight: Color
    abstract val onErrorContainerLight: Color
    abstract val backgroundLight: Color
    abstract val onBackgroundLight: Color
    abstract val surfaceLight: Color
    abstract val onSurfaceLight: Color
    abstract val surfaceVariantLight: Color
    abstract val onSurfaceVariantLight: Color
    abstract val outlineLight: Color
    abstract val outlineVariantLight: Color
    abstract val scrimLight: Color
    abstract val inverseSurfaceLight: Color
    abstract val inverseOnSurfaceLight: Color
    abstract val inversePrimaryLight: Color
    abstract val surfaceDimLight: Color
    abstract val surfaceBrightLight: Color
    abstract val surfaceContainerLowestLight: Color
    abstract val surfaceContainerLowLight: Color
    abstract val surfaceContainerLight: Color
    abstract val surfaceContainerHighLight: Color
    abstract val surfaceContainerHighestLight: Color
    // Dark theme
    abstract val primaryDark: Color
    abstract val onPrimaryDark: Color
    abstract val primaryContainerDark: Color
    abstract val onPrimaryContainerDark: Color
    abstract val secondaryDark: Color
    abstract val onSecondaryDark: Color
    abstract val secondaryContainerDark: Color
    abstract val onSecondaryContainerDark: Color
    abstract val tertiaryDark: Color
    abstract val onTertiaryDark: Color
    abstract val tertiaryContainerDark: Color
    abstract val onTertiaryContainerDark: Color
    abstract val errorDark: Color
    abstract val onErrorDark: Color
    abstract val errorContainerDark: Color
    abstract val onErrorContainerDark: Color
    abstract val backgroundDark: Color
    abstract val onBackgroundDark: Color
    abstract val surfaceDark: Color
    abstract val onSurfaceDark: Color
    abstract val surfaceVariantDark: Color
    abstract val onSurfaceVariantDark: Color
    abstract val outlineDark: Color
    abstract val outlineVariantDark: Color
    abstract val scrimDark: Color
    abstract val inverseSurfaceDark: Color
    abstract val inverseOnSurfaceDark: Color
    abstract val inversePrimaryDark: Color
    abstract val surfaceDimDark: Color
    abstract val surfaceBrightDark: Color
    abstract val surfaceContainerLowestDark: Color
    abstract val surfaceContainerLowDark: Color
    abstract val surfaceContainerDark: Color
    abstract val surfaceContainerHighDark: Color
    abstract val surfaceContainerHighestDark: Color

    object Default : ThemeColors() {
        override val primaryLight = Color(0xFF396B9D)
        override val onPrimaryLight = Color(0xFFFFFFFF)
        override val primaryContainerLight = Color(0xFFE0EDF8)
        override val onPrimaryContainerLight = Color(0xFF224D76)
        override val secondaryLight = Color(0xFF536D83)
        override val onSecondaryLight = Color(0xFFFFFFFF)
        override val secondaryContainerLight = Color(0xFFE6EDF3)
        override val onSecondaryContainerLight = Color(0xFF354E62)
        override val tertiaryLight = Color(0xFF4F746F)
        override val onTertiaryLight = Color(0xFFFFFFFF)
        override val tertiaryContainerLight = Color(0xFFE0EFEB)
        override val onTertiaryContainerLight = Color(0xFF315650)
        override val errorLight = Color(0xFFBA1A1A)
        override val onErrorLight = Color(0xFFFFFFFF)
        override val errorContainerLight = Color(0xFFFFDAD6)
        override val onErrorContainerLight = Color(0xFF93000A)
        override val backgroundLight = Color(0xFFF9F9FF)
        override val onBackgroundLight = Color(0xFF191C20)
        override val surfaceLight = Color(0xFFF9F9FF)
        override val onSurfaceLight = Color(0xFF191C20)
        override val surfaceVariantLight = Color(0xFFE0E2EC)
        override val onSurfaceVariantLight = Color(0xFF44474E)
        override val outlineLight = Color(0xFF74777F)
        override val outlineVariantLight = Color(0xFFC4C6D0)
        override val scrimLight = Color(0xFF000000)
        override val inverseSurfaceLight = Color(0xFF2E3036)
        override val inverseOnSurfaceLight = Color(0xFFF0F0F7)
        override val inversePrimaryLight = Color(0xFFA3C8EB)
        override val surfaceDimLight = Color(0xFFD9D9E0)
        override val surfaceBrightLight = Color(0xFFF9F9FF)
        override val surfaceContainerLowestLight = Color(0xFFFFFFFF)
        override val surfaceContainerLowLight = Color(0xFFF3F3FA)
        override val surfaceContainerLight = Color(0xFFEDEDF4)
        override val surfaceContainerHighLight = Color(0xFFE7E8EE)
        override val surfaceContainerHighestLight = Color(0xFFE2E2E9)

        override val primaryDark = Color(0xFFA3C8EB)
        override val onPrimaryDark = Color(0xFF133653)
        override val primaryContainerDark = Color(0xFF284C6A)
        override val onPrimaryContainerDark = Color(0xFFDDEDFB)
        override val secondaryDark = Color(0xFFB8CBDC)
        override val onSecondaryDark = Color(0xFF263B4B)
        override val secondaryContainerDark = Color(0xFF3A4F60)
        override val onSecondaryContainerDark = Color(0xFFE0EBF4)
        override val tertiaryDark = Color(0xFFAACFC6)
        override val onTertiaryDark = Color(0xFF193C35)
        override val tertiaryContainerDark = Color(0xFF34574F)
        override val onTertiaryContainerDark = Color(0xFFDCEFE8)
        override val errorDark = Color(0xFFFFB4AB)
        override val onErrorDark = Color(0xFF690005)
        override val errorContainerDark = Color(0xFF93000A)
        override val onErrorContainerDark = Color(0xFFFFDAD6)
        override val backgroundDark = Color(0xFF111318)
        override val onBackgroundDark = Color(0xFFE2E2E9)
        override val surfaceDark = Color(0xFF111318)
        override val onSurfaceDark = Color(0xFFE2E2E9)
        override val surfaceVariantDark = Color(0xFF44474E)
        override val onSurfaceVariantDark = Color(0xFFC4C6D0)
        override val outlineDark = Color(0xFF8E9099)
        override val outlineVariantDark = Color(0xFF44474E)
        override val scrimDark = Color(0xFF000000)
        override val inverseSurfaceDark = Color(0xFFE2E2E9)
        override val inverseOnSurfaceDark = Color(0xFF2E3036)
        override val inversePrimaryDark = Color(0xFF396B9D)
        override val surfaceDimDark = Color(0xFF111318)
        override val surfaceBrightDark = Color(0xFF37393E)
        override val surfaceContainerLowestDark = Color(0xFF0C0E13)
        override val surfaceContainerLowDark = Color(0xFF191C20)
        override val surfaceContainerDark = Color(0xFF1D2024)
        override val surfaceContainerHighDark = Color(0xFF282A2F)
        override val surfaceContainerHighestDark = Color(0xFF33353A)
    }

    object Green : ThemeColors() {
        override val primaryLight = Color(0xFF168153)
        override val onPrimaryLight = Color(0xFFFFFFFF)
        override val primaryContainerLight = Color(0xFFDDF0E5)
        override val onPrimaryContainerLight = Color(0xFF185D3D)
        override val secondaryLight = Color(0xFF567563)
        override val onSecondaryLight = Color(0xFFFFFFFF)
        override val secondaryContainerLight = Color(0xFFE6EFE8)
        override val onSecondaryContainerLight = Color(0xFF355642)
        override val tertiaryLight = Color(0xFF477A70)
        override val onTertiaryLight = Color(0xFFFFFFFF)
        override val tertiaryContainerLight = Color(0xFFE0F0EB)
        override val onTertiaryContainerLight = Color(0xFF285C51)
        override val errorLight = Color(0xFFBA1A1A)
        override val onErrorLight = Color(0xFFFFFFFF)
        override val errorContainerLight = Color(0xFFFFDAD6)
        override val onErrorContainerLight = Color(0xFF93000A)
        override val backgroundLight = Color(0xFFF9FAEF)
        override val onBackgroundLight = Color(0xFF1A1C16)
        override val surfaceLight = Color(0xFFF9FAEF)
        override val onSurfaceLight = Color(0xFF1A1C16)
        override val surfaceVariantLight = Color(0xFFE1E4D5)
        override val onSurfaceVariantLight = Color(0xFF44483D)
        override val outlineLight = Color(0xFF75796C)
        override val outlineVariantLight = Color(0xFFC5C8BA)
        override val scrimLight = Color(0xFF000000)
        override val inverseSurfaceLight = Color(0xFF2F312A)
        override val inverseOnSurfaceLight = Color(0xFFF1F2E6)
        override val inversePrimaryLight = Color(0xFF8AD7AE)
        override val surfaceDimLight = Color(0xFFDADBD0)
        override val surfaceBrightLight = Color(0xFFF9FAEF)
        override val surfaceContainerLowestLight = Color(0xFFFFFFFF)
        override val surfaceContainerLowLight = Color(0xFFF3F4E9)
        override val surfaceContainerLight = Color(0xFFEEEFE3)
        override val surfaceContainerHighLight = Color(0xFFE8E9DE)
        override val surfaceContainerHighestLight = Color(0xFFE2E3D8)

        override val primaryDark = Color(0xFF8AD7AE)
        override val onPrimaryDark = Color(0xFF073C25)
        override val primaryContainerDark = Color(0xFF20533A)
        override val onPrimaryContainerDark = Color(0xFFD7F4E3)
        override val secondaryDark = Color(0xFFB5D1BC)
        override val onSecondaryDark = Color(0xFF213D2B)
        override val secondaryContainerDark = Color(0xFF3A5643)
        override val onSecondaryContainerDark = Color(0xFFDFEEE3)
        override val tertiaryDark = Color(0xFFA0D4C5)
        override val onTertiaryDark = Color(0xFF103D32)
        override val tertiaryContainerDark = Color(0xFF2B594C)
        override val onTertiaryContainerDark = Color(0xFFD8F2E9)
        override val errorDark = Color(0xFFFFB4AB)
        override val onErrorDark = Color(0xFF690005)
        override val errorContainerDark = Color(0xFF93000A)
        override val onErrorContainerDark = Color(0xFFFFDAD6)
        override val backgroundDark = Color(0xFF12140E)
        override val onBackgroundDark = Color(0xFFE2E3D8)
        override val surfaceDark = Color(0xFF12140E)
        override val onSurfaceDark = Color(0xFFE2E3D8)
        override val surfaceVariantDark = Color(0xFF44483D)
        override val onSurfaceVariantDark = Color(0xFFC5C8BA)
        override val outlineDark = Color(0xFF8F9285)
        override val outlineVariantDark = Color(0xFF44483D)
        override val scrimDark = Color(0xFF000000)
        override val inverseSurfaceDark = Color(0xFFE2E3D8)
        override val inverseOnSurfaceDark = Color(0xFF2F312A)
        override val inversePrimaryDark = Color(0xFF168153)
        override val surfaceDimDark = Color(0xFF12140E)
        override val surfaceBrightDark = Color(0xFF383A32)
        override val surfaceContainerLowestDark = Color(0xFF0C0F09)
        override val surfaceContainerLowDark = Color(0xFF1A1C16)
        override val surfaceContainerDark = Color(0xFF1E201A)
        override val surfaceContainerHighDark = Color(0xFF282B24)
        override val surfaceContainerHighestDark = Color(0xFF33362E)
    }

    object Purple : ThemeColors() {
        override val primaryLight = Color(0xFF79609E)
        override val onPrimaryLight = Color(0xFFFFFFFF)
        override val primaryContainerLight = Color(0xFFEEE6F6)
        override val onPrimaryContainerLight = Color(0xFF553D76)
        override val secondaryLight = Color(0xFF70657E)
        override val onSecondaryLight = Color(0xFFFFFFFF)
        override val secondaryContainerLight = Color(0xFFEDE8F1)
        override val onSecondaryContainerLight = Color(0xFF51455F)
        override val tertiaryLight = Color(0xFF8B647C)
        override val onTertiaryLight = Color(0xFFFFFFFF)
        override val tertiaryContainerLight = Color(0xFFF5E5EF)
        override val onTertiaryContainerLight = Color(0xFF68435A)
        override val errorLight = Color(0xFFBA1A1A)
        override val onErrorLight = Color(0xFFFFFFFF)
        override val errorContainerLight = Color(0xFFFFDAD6)
        override val onErrorContainerLight = Color(0xFF93000A)
        override val backgroundLight = Color(0xFFFFF7FA)
        override val onBackgroundLight = Color(0xFF1F1A1F)
        override val surfaceLight = Color(0xFFFFF7FA)
        override val onSurfaceLight = Color(0xFF1F1A1F)
        override val surfaceVariantLight = Color(0xFFEDDFE8)
        override val onSurfaceVariantLight = Color(0xFF4D444C)
        override val outlineLight = Color(0xFF7F747C)
        override val outlineVariantLight = Color(0xFFD0C3CC)
        override val scrimLight = Color(0xFF000000)
        override val inverseSurfaceLight = Color(0xFF352F34)
        override val inverseOnSurfaceLight = Color(0xFFF9EEF4)
        override val inversePrimaryLight = Color(0xFFC9B5E8)
        override val surfaceDimLight = Color(0xFFE2D7DE)
        override val surfaceBrightLight = Color(0xFFFFF7FA)
        override val surfaceContainerLowestLight = Color(0xFFFFFFFF)
        override val surfaceContainerLowLight = Color(0xFFFCF0F7)
        override val surfaceContainerLight = Color(0xFFF6EBF2)
        override val surfaceContainerHighLight = Color(0xFFF0E5EC)
        override val surfaceContainerHighestLight = Color(0xFFEBDFE6)

        override val primaryDark = Color(0xFFC9B5E8)
        override val onPrimaryDark = Color(0xFF36244F)
        override val primaryContainerDark = Color(0xFF514067)
        override val onPrimaryContainerDark = Color(0xFFEEE2FB)
        override val secondaryDark = Color(0xFFCABDD8)
        override val onSecondaryDark = Color(0xFF352B41)
        override val secondaryContainerDark = Color(0xFF4D415A)
        override val onSecondaryContainerDark = Color(0xFFEDE4F6)
        override val tertiaryDark = Color(0xFFE2B9D0)
        override val onTertiaryDark = Color(0xFF482A3C)
        override val tertiaryContainerDark = Color(0xFF634353)
        override val onTertiaryContainerDark = Color(0xFFF9E1EE)
        override val errorDark = Color(0xFFFFB4AB)
        override val onErrorDark = Color(0xFF690005)
        override val errorContainerDark = Color(0xFF93000A)
        override val onErrorContainerDark = Color(0xFFFFDAD6)
        override val backgroundDark = Color(0xFF171216)
        override val onBackgroundDark = Color(0xFFEBDFE6)
        override val surfaceDark = Color(0xFF171216)
        override val onSurfaceDark = Color(0xFFEBDFE6)
        override val surfaceVariantDark = Color(0xFF4D444C)
        override val onSurfaceVariantDark = Color(0xFFD0C3CC)
        override val outlineDark = Color(0xFF998D96)
        override val outlineVariantDark = Color(0xFF4D444C)
        override val scrimDark = Color(0xFF000000)
        override val inverseSurfaceDark = Color(0xFFEBDFE6)
        override val inverseOnSurfaceDark = Color(0xFF352F34)
        override val inversePrimaryDark = Color(0xFF79609E)
        override val surfaceDimDark = Color(0xFF171216)
        override val surfaceBrightDark = Color(0xFF3E373D)
        override val surfaceContainerLowestDark = Color(0xFF110D11)
        override val surfaceContainerLowDark = Color(0xFF1F1A1F)
        override val surfaceContainerDark = Color(0xFF231E23)
        override val surfaceContainerHighDark = Color(0xFF2E282D)
        override val surfaceContainerHighestDark = Color(0xFF393338)
    }

    object Orange : ThemeColors() {
        override val primaryLight = Color(0xFFA15F32)
        override val onPrimaryLight = Color(0xFFFFFFFF)
        override val primaryContainerLight = Color(0xFFF8E8DA)
        override val onPrimaryContainerLight = Color(0xFF75421F)
        override val secondaryLight = Color(0xFF806B56)
        override val onSecondaryLight = Color(0xFFFFFFFF)
        override val secondaryContainerLight = Color(0xFFF1EAE2)
        override val onSecondaryContainerLight = Color(0xFF5C4836)
        override val tertiaryLight = Color(0xFF8F7041)
        override val onTertiaryLight = Color(0xFFFFFFFF)
        override val tertiaryContainerLight = Color(0xFFF5EBD8)
        override val onTertiaryContainerLight = Color(0xFF694D27)
        override val errorLight = Color(0xFFBA1A1A)
        override val onErrorLight = Color(0xFFFFFFFF)
        override val errorContainerLight = Color(0xFFFFDAD6)
        override val onErrorContainerLight = Color(0xFF93000A)
        override val backgroundLight = Color(0xFFFFF8F5)
        override val onBackgroundLight = Color(0xFF221A15)
        override val surfaceLight = Color(0xFFFFF8F5)
        override val onSurfaceLight = Color(0xFF221A15)
        override val surfaceVariantLight = Color(0xFFF4DED3)
        override val onSurfaceVariantLight = Color(0xFF52443C)
        override val outlineLight = Color(0xFF84746A)
        override val outlineVariantLight = Color(0xFFD7C3B8)
        override val scrimLight = Color(0xFF000000)
        override val inverseSurfaceLight = Color(0xFF382E29)
        override val inverseOnSurfaceLight = Color(0xFFFFEDE5)
        override val inversePrimaryLight = Color(0xFFEDB88F)
        override val surfaceDimLight = Color(0xFFE7D7CE)
        override val surfaceBrightLight = Color(0xFFFFF8F5)
        override val surfaceContainerLowestLight = Color(0xFFFFFFFF)
        override val surfaceContainerLowLight = Color(0xFFFFF1EA)
        override val surfaceContainerLight = Color(0xFFFCEBE2)
        override val surfaceContainerHighLight = Color(0xFFF6E5DC)
        override val surfaceContainerHighestLight = Color(0xFFF0DFD7)

        override val primaryDark = Color(0xFFEDB88F)
        override val onPrimaryDark = Color(0xFF4D2B13)
        override val primaryContainerDark = Color(0xFF6A442A)
        override val onPrimaryContainerDark = Color(0xFFFAE5D4)
        override val secondaryDark = Color(0xFFD7C2AA)
        override val onSecondaryDark = Color(0xFF403224)
        override val secondaryContainerDark = Color(0xFF584838)
        override val onSecondaryContainerDark = Color(0xFFF2E6D8)
        override val tertiaryDark = Color(0xFFDCC497)
        override val onTertiaryDark = Color(0xFF403118)
        override val tertiaryContainerDark = Color(0xFF5C4B30)
        override val onTertiaryContainerDark = Color(0xFFF5E9D1)
        override val errorDark = Color(0xFFFFB4AB)
        override val onErrorDark = Color(0xFF690005)
        override val errorContainerDark = Color(0xFF93000A)
        override val onErrorContainerDark = Color(0xFFFFDAD6)
        override val backgroundDark = Color(0xFF19120D)
        override val onBackgroundDark = Color(0xFFF0DFD7)
        override val surfaceDark = Color(0xFF19120D)
        override val onSurfaceDark = Color(0xFFF0DFD7)
        override val surfaceVariantDark = Color(0xFF52443C)
        override val onSurfaceVariantDark = Color(0xFFD7C3B8)
        override val outlineDark = Color(0xFF9F8D83)
        override val outlineVariantDark = Color(0xFF52443C)
        override val scrimDark = Color(0xFF000000)
        override val inverseSurfaceDark = Color(0xFFF0DFD7)
        override val inverseOnSurfaceDark = Color(0xFF382E29)
        override val inversePrimaryDark = Color(0xFFA15F32)
        override val surfaceDimDark = Color(0xFF19120D)
        override val surfaceBrightDark = Color(0xFF413731)
        override val surfaceContainerLowestDark = Color(0xFF140D08)
        override val surfaceContainerLowDark = Color(0xFF221A15)
        override val surfaceContainerDark = Color(0xFF261E19)
        override val surfaceContainerHighDark = Color(0xFF312823)
        override val surfaceContainerHighestDark = Color(0xFF3D332D)
    }

    object Pink : ThemeColors() {
        override val primaryLight = Color(0xFFA15C77)
        override val onPrimaryLight = Color(0xFFFFFFFF)
        override val primaryContainerLight = Color(0xFFF8E5ED)
        override val onPrimaryContainerLight = Color(0xFF773F55)
        override val secondaryLight = Color(0xFF806572)
        override val onSecondaryLight = Color(0xFFFFFFFF)
        override val secondaryContainerLight = Color(0xFFF1E8ED)
        override val onSecondaryContainerLight = Color(0xFF5F4653)
        override val tertiaryLight = Color(0xFF98685C)
        override val onTertiaryLight = Color(0xFFFFFFFF)
        override val tertiaryContainerLight = Color(0xFFF7E8E1)
        override val onTertiaryContainerLight = Color(0xFF70483E)
        override val errorLight = Color(0xFFBA1A1A)
        override val onErrorLight = Color(0xFFFFFFFF)
        override val errorContainerLight = Color(0xFFFFDAD6)
        override val onErrorContainerLight = Color(0xFF93000A)
        override val backgroundLight = Color(0xFFFFF8F8)
        override val onBackgroundLight = Color(0xFF22191B)
        override val surfaceLight = Color(0xFFFFF8F8)
        override val onSurfaceLight = Color(0xFF22191B)
        override val surfaceVariantLight = Color(0xFFF2DDE1)
        override val onSurfaceVariantLight = Color(0xFF514346)
        override val outlineLight = Color(0xFF837377)
        override val outlineVariantLight = Color(0xFFD5C2C5)
        override val scrimLight = Color(0xFF000000)
        override val inverseSurfaceLight = Color(0xFF372E30)
        override val inverseOnSurfaceLight = Color(0xFFFDEDEF)
        override val inversePrimaryLight = Color(0xFFE8B2C8)
        override val surfaceDimLight = Color(0xFFE6D6D9)
        override val surfaceBrightLight = Color(0xFFFFF8F8)
        override val surfaceContainerLowestLight = Color(0xFFFFFFFF)
        override val surfaceContainerLowLight = Color(0xFFFFF0F2)
        override val surfaceContainerLight = Color(0xFFFBEAED)
        override val surfaceContainerHighLight = Color(0xFFF5E4E7)
        override val surfaceContainerHighestLight = Color(0xFFEFDFE1)

        override val primaryDark = Color(0xFFE8B2C8)
        override val onPrimaryDark = Color(0xFF4D2537)
        override val primaryContainerDark = Color(0xFF6A3F50)
        override val onPrimaryContainerDark = Color(0xFFFBE1EC)
        override val secondaryDark = Color(0xFFD8BDCA)
        override val onSecondaryDark = Color(0xFF412C37)
        override val secondaryContainerDark = Color(0xFF59424E)
        override val onSecondaryContainerDark = Color(0xFFF2E3EB)
        override val tertiaryDark = Color(0xFFE2BEAF)
        override val onTertiaryDark = Color(0xFF492E25)
        override val tertiaryContainerDark = Color(0xFF63483C)
        override val onTertiaryContainerDark = Color(0xFFF8E7DE)
        override val errorDark = Color(0xFFFFB4AB)
        override val onErrorDark = Color(0xFF690005)
        override val errorContainerDark = Color(0xFF93000A)
        override val onErrorContainerDark = Color(0xFFFFDAD6)
        override val backgroundDark = Color(0xFF191113)
        override val onBackgroundDark = Color(0xFFEFDFE1)
        override val surfaceDark = Color(0xFF191113)
        override val onSurfaceDark = Color(0xFFEFDFE1)
        override val surfaceVariantDark = Color(0xFF514346)
        override val onSurfaceVariantDark = Color(0xFFD5C2C5)
        override val outlineDark = Color(0xFF9E8C90)
        override val outlineVariantDark = Color(0xFF514346)
        override val scrimDark = Color(0xFF000000)
        override val inverseSurfaceDark = Color(0xFFEFDFE1)
        override val inverseOnSurfaceDark = Color(0xFF372E30)
        override val inversePrimaryDark = Color(0xFFA15C77)
        override val surfaceDimDark = Color(0xFF191113)
        override val surfaceBrightDark = Color(0xFF413739)
        override val surfaceContainerLowestDark = Color(0xFF140C0E)
        override val surfaceContainerLowDark = Color(0xFF22191B)
        override val surfaceContainerDark = Color(0xFF261D1F)
        override val surfaceContainerHighDark = Color(0xFF31282A)
        override val surfaceContainerHighestDark = Color(0xFF3C3234)
    }

    object Gray : ThemeColors() {
        override val primaryLight = Color(0xFF626C70)
        override val onPrimaryLight = Color(0xFFFFFFFF)
        override val primaryContainerLight = Color(0xFFE8ECEE)
        override val onPrimaryContainerLight = Color(0xFF414D53)
        override val secondaryLight = Color(0xFF686F70)
        override val onSecondaryLight = Color(0xFFFFFFFF)
        override val secondaryContainerLight = Color(0xFFEBEFEF)
        override val onSecondaryContainerLight = Color(0xFF485153)
        override val tertiaryLight = Color(0xFF63717B)
        override val onTertiaryLight = Color(0xFFFFFFFF)
        override val tertiaryContainerLight = Color(0xFFE6EDF2)
        override val onTertiaryContainerLight = Color(0xFF424F5A)
        override val errorLight = Color(0xFFBA1A1A)
        override val onErrorLight = Color(0xFFFFFFFF)
        override val errorContainerLight = Color(0xFFFFDAD6)
        override val onErrorContainerLight = Color(0xFF93000A)
        override val backgroundLight = Color(0xFFFCF8F8)
        override val onBackgroundLight = Color(0xFF1C1B1B)
        override val surfaceLight = Color(0xFFFCF8F8)
        override val onSurfaceLight = Color(0xFF1C1B1B)
        override val surfaceVariantLight = Color(0xFFE0E3E3)
        override val onSurfaceVariantLight = Color(0xFF444748)
        override val outlineLight = Color(0xFF747878)
        override val outlineVariantLight = Color(0xFFC4C7C7)
        override val scrimLight = Color(0xFF000000)
        override val inverseSurfaceLight = Color(0xFF313030)
        override val inverseOnSurfaceLight = Color(0xFFF4F0EF)
        override val inversePrimaryLight = Color(0xFFC0CACF)
        override val surfaceDimLight = Color(0xFFDDD9D8)
        override val surfaceBrightLight = Color(0xFFFCF8F8)
        override val surfaceContainerLowestLight = Color(0xFFFFFFFF)
        override val surfaceContainerLowLight = Color(0xFFF7F3F2)
        override val surfaceContainerLight = Color(0xFFF1EDEC)
        override val surfaceContainerHighLight = Color(0xFFEBE7E7)
        override val surfaceContainerHighestLight = Color(0xFFE5E2E1)

        override val primaryDark = Color(0xFFC0CACF)
        override val onPrimaryDark = Color(0xFF293439)
        override val primaryContainerDark = Color(0xFF424E54)
        override val onPrimaryContainerDark = Color(0xFFE7EDF0)
        override val secondaryDark = Color(0xFFC5CECE)
        override val onSecondaryDark = Color(0xFF2E3738)
        override val secondaryContainerDark = Color(0xFF475152)
        override val onSecondaryContainerDark = Color(0xFFEAF0F0)
        override val tertiaryDark = Color(0xFFBDCBD5)
        override val onTertiaryDark = Color(0xFF29353F)
        override val tertiaryContainerDark = Color(0xFF424F5B)
        override val onTertiaryContainerDark = Color(0xFFE5EDF4)
        override val errorDark = Color(0xFFFFB4AB)
        override val onErrorDark = Color(0xFF690005)
        override val errorContainerDark = Color(0xFF93000A)
        override val onErrorContainerDark = Color(0xFFFFDAD6)
        override val backgroundDark = Color(0xFF141313)
        override val onBackgroundDark = Color(0xFFE5E2E1)
        override val surfaceDark = Color(0xFF141313)
        override val onSurfaceDark = Color(0xFFE5E2E1)
        override val surfaceVariantDark = Color(0xFF444748)
        override val onSurfaceVariantDark = Color(0xFFC4C7C7)
        override val outlineDark = Color(0xFF8E9192)
        override val outlineVariantDark = Color(0xFF444748)
        override val scrimDark = Color(0xFF000000)
        override val inverseSurfaceDark = Color(0xFFE5E2E1)
        override val inverseOnSurfaceDark = Color(0xFF313030)
        override val inversePrimaryDark = Color(0xFF626C70)
        override val surfaceDimDark = Color(0xFF141313)
        override val surfaceBrightDark = Color(0xFF3A3939)
        override val surfaceContainerLowestDark = Color(0xFF0E0E0E)
        override val surfaceContainerLowDark = Color(0xFF1C1B1B)
        override val surfaceContainerDark = Color(0xFF201F1F)
        override val surfaceContainerHighDark = Color(0xFF2A2A2A)
        override val surfaceContainerHighestDark = Color(0xFF353434)
    }

    object Yellow : ThemeColors() {
        override val primaryLight = Color(0xFF886C24)
        override val onPrimaryLight = Color(0xFFFFFFFF)
        override val primaryContainerLight = Color(0xFFF5EDCF)
        override val onPrimaryContainerLight = Color(0xFF645016)
        override val secondaryLight = Color(0xFF797050)
        override val onSecondaryLight = Color(0xFFFFFFFF)
        override val secondaryContainerLight = Color(0xFFF0ECDF)
        override val onSecondaryContainerLight = Color(0xFF575036)
        override val tertiaryLight = Color(0xFF747848)
        override val onTertiaryLight = Color(0xFFFFFFFF)
        override val tertiaryContainerLight = Color(0xFFEDF0DB)
        override val onTertiaryContainerLight = Color(0xFF50552D)
        override val errorLight = Color(0xFFBA1A1A)
        override val onErrorLight = Color(0xFFFFFFFF)
        override val errorContainerLight = Color(0xFFFFDAD6)
        override val onErrorContainerLight = Color(0xFF93000A)
        override val backgroundLight = Color(0xFFFFF9ED)
        override val onBackgroundLight = Color(0xFF1E1C13)
        override val surfaceLight = Color(0xFFFFF9ED)
        override val onSurfaceLight = Color(0xFF1E1C13)
        override val surfaceVariantLight = Color(0xFFE9E2D0)
        override val onSurfaceVariantLight = Color(0xFF4B4739)
        override val outlineLight = Color(0xFF7C7768)
        override val outlineVariantLight = Color(0xFFCDC6B4)
        override val scrimLight = Color(0xFF000000)
        override val inverseSurfaceLight = Color(0xFF333027)
        override val inverseOnSurfaceLight = Color(0xFFF7F0E2)
        override val inversePrimaryLight = Color(0xFFDCC88E)
        override val surfaceDimLight = Color(0xFFE0D9CC)
        override val surfaceBrightLight = Color(0xFFFFF9ED)
        override val surfaceContainerLowestLight = Color(0xFFFFFFFF)
        override val surfaceContainerLowLight = Color(0xFFFAF3E5)
        override val surfaceContainerLight = Color(0xFFF4EDDF)
        override val surfaceContainerHighLight = Color(0xFFEEE8DA)
        override val surfaceContainerHighestLight = Color(0xFFE8E2D4)

        override val primaryDark = Color(0xFFDCC88E)
        override val onPrimaryDark = Color(0xFF3C3010)
        override val primaryContainerDark = Color(0xFF58491F)
        override val onPrimaryContainerDark = Color(0xFFF5EBCB)
        override val secondaryDark = Color(0xFFD2C9A9)
        override val onSecondaryDark = Color(0xFF383320)
        override val secondaryContainerDark = Color(0xFF514B35)
        override val onSecondaryContainerDark = Color(0xFFF0EAD7)
        override val tertiaryDark = Color(0xFFCAD1A4)
        override val onTertiaryDark = Color(0xFF2F361B)
        override val tertiaryContainerDark = Color(0xFF4A5133)
        override val onTertiaryContainerDark = Color(0xFFECF1D9)
        override val errorDark = Color(0xFFFFB4AB)
        override val onErrorDark = Color(0xFF690005)
        override val errorContainerDark = Color(0xFF93000A)
        override val onErrorContainerDark = Color(0xFFFFDAD6)
        override val backgroundDark = Color(0xFF15130B)
        override val onBackgroundDark = Color(0xFFE8E2D4)
        override val surfaceDark = Color(0xFF15130B)
        override val onSurfaceDark = Color(0xFFE8E2D4)
        override val surfaceVariantDark = Color(0xFF4B4739)
        override val onSurfaceVariantDark = Color(0xFFCDC6B4)
        override val outlineDark = Color(0xFF969080)
        override val outlineVariantDark = Color(0xFF4B4739)
        override val scrimDark = Color(0xFF000000)
        override val inverseSurfaceDark = Color(0xFFE8E2D4)
        override val inverseOnSurfaceDark = Color(0xFF333027)
        override val inversePrimaryDark = Color(0xFF886C24)
        override val surfaceDimDark = Color(0xFF15130B)
        override val surfaceBrightDark = Color(0xFF3C3930)
        override val surfaceContainerLowestDark = Color(0xFF100E07)
        override val surfaceContainerLowDark = Color(0xFF1E1C13)
        override val surfaceContainerDark = Color(0xFF222017)
        override val surfaceContainerHighDark = Color(0xFF2C2A21)
        override val surfaceContainerHighestDark = Color(0xFF37352B)
    }

    companion object {
        fun fromName(name: String): ThemeColors = when (name.lowercase()) {
            "green" -> Green
            "purple" -> Purple
            "orange" -> Orange
            "pink" -> Pink
            "gray" -> Gray
            "yellow" -> Yellow
            "trans", "transright" -> TransPride
            else -> Default
        }
    }

    object TransPride : ThemeColors() {
        private val c1 = Color(0xFF5BCFFA)
        private val c2 = Color(0xFFF5ABB9)
        private val c3 = Color(0xFFFFFFFF)
        private val c1l = Color(0xFF91E0FF)
        private val c1m = Color(0xFF3BB8E8)
        private val c1d = Color(0xFF0D7FAD)
        private val c1x = Color(0xFF065A7A)
        private val c2l = Color(0xFFFFD4E0)
        private val c2m = Color(0xFFE896AB)
        private val c2d = Color(0xFFBF6B83)
        private val c2x = Color(0xFF8E4A5E)
        private val t1 = Color(0xFFC8B6FF)
        private val t1l = Color(0xFFE8DEFF)
        private val t1d = Color(0xFF9B7FD1)
        private val s1 = Color(0xFFF0FAFF)
        private val s2 = Color(0xFFFFF5F8)
        private val ds = Color(0xFF16141A)
        override val primaryLight = c1m
        override val onPrimaryLight = c3
        override val primaryContainerLight = Color(0xFFCCF0FF)
        override val onPrimaryContainerLight = c1x
        override val secondaryLight = c2m
        override val onSecondaryLight = c3
        override val secondaryContainerLight = Color(0xFFFFE4EC)
        override val onSecondaryContainerLight = c2x
        override val tertiaryLight = t1d
        override val onTertiaryLight = c3
        override val tertiaryContainerLight = t1l
        override val onTertiaryContainerLight = Color(0xFF3D2E66)
        override val errorLight = Color(0xFFBA1A1A)
        override val onErrorLight = c3
        override val errorContainerLight = Color(0xFFFFDAD6)
        override val onErrorContainerLight = Color(0xFF410002)
        override val backgroundLight = c3
        override val onBackgroundLight = Color(0xFF1A1B1F)
        override val surfaceLight = c3
        override val onSurfaceLight = Color(0xFF1A1B1F)
        override val surfaceVariantLight = Color(0xFFE8E0F0)
        override val onSurfaceVariantLight = Color(0xFF49454F)
        override val outlineLight = c2m.copy(alpha = 0.5f)
        override val outlineVariantLight = c1l.copy(alpha = 0.6f)
        override val scrimLight = Color(0xFF000000)
        override val inverseSurfaceLight = ds
        override val inverseOnSurfaceLight = Color(0xFFF4EFF4)
        override val inversePrimaryLight = c1l
        override val surfaceDimLight = Color(0xFFDDD8E4)
        override val surfaceBrightLight = c3
        override val surfaceContainerLowestLight = c3
        override val surfaceContainerLowLight = s2
        override val surfaceContainerLight = Color(0xFFF8F2FA)
        override val surfaceContainerHighLight = Color(0xFFF2ECF8)
        override val surfaceContainerHighestLight = Color(0xFFECE6F3)
        override val primaryDark = c1
        override val onPrimaryDark = c1x
        override val primaryContainerDark = c1d
        override val onPrimaryContainerDark = c1l
        override val secondaryDark = c2
        override val onSecondaryDark = c2x
        override val secondaryContainerDark = c2d
        override val onSecondaryContainerDark = c2l
        override val tertiaryDark = t1
        override val onTertiaryDark = Color(0xFF2D1F4E)
        override val tertiaryContainerDark = Color(0xFF453670)
        override val onTertiaryContainerDark = t1l
        override val errorDark = Color(0xFFFFB4AB)
        override val onErrorDark = Color(0xFF690005)
        override val errorContainerDark = Color(0xFF93000A)
        override val onErrorContainerDark = Color(0xFFFFDAD6)
        override val backgroundDark = ds
        override val onBackgroundDark = Color(0xFFE6E1E6)
        override val surfaceDark = ds
        override val onSurfaceDark = Color(0xFFE6E1E6)
        override val surfaceVariantDark = Color(0xFF49454F)
        override val onSurfaceVariantDark = Color(0xFFCBC4CF)
        override val outlineDark = c1.copy(alpha = 0.6f)
        override val outlineVariantDark = c2.copy(alpha = 0.4f)
        override val scrimDark = Color(0xFF000000)
        override val inverseSurfaceDark = Color(0xFFE6E1E6)
        override val inverseOnSurfaceDark = Color(0xFF313033)
        override val inversePrimaryDark = c1m
        override val surfaceDimDark = ds
        override val surfaceBrightDark = Color(0xFF3B383E)
        override val surfaceContainerLowestDark = Color(0xFF0F0D12)
        override val surfaceContainerLowDark = Color(0xFF1C1A21)
        override val surfaceContainerDark = Color(0xFF201E25)
        override val surfaceContainerHighDark = Color(0xFF2B2830)
        override val surfaceContainerHighestDark = Color(0xFF36333B)
    }
}