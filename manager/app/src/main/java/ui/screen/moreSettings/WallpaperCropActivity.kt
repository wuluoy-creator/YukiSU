package ui.screen.moreSettings

import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.text.TextUtils
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.appcompat.widget.Toolbar
import androidx.core.graphics.ColorUtils
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.zying.zysu.R
import com.yalantis.ucrop.UCrop
import com.yalantis.ucrop.UCropActivity
import kotlin.math.roundToInt

/** Keeps uCrop's editing and result contract while matching the manager's inset capsule header. */
class WallpaperCropActivity : UCropActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val toolbar = findViewById<Toolbar>(com.yalantis.ucrop.R.id.toolbar) ?: return
        val density = resources.displayMetrics.density
        fun dp(value: Float) = (value * density).roundToInt()
        val color = intent.getIntExtra(UCrop.Options.EXTRA_TOOL_BAR_COLOR, Color.WHITE)
        val dark = ColorUtils.calculateLuminance(color) < 0.5
        val height = dp(64f + 16f * (resources.configuration.fontScale - 1f).coerceIn(0f, 1.5f))

        findViewById<View>(com.yalantis.ucrop.R.id.ucrop_photobox).setBackgroundColor(color)
        toolbar.minimumHeight = height
        toolbar.setPadding(dp(6f), 0, dp(6f), 0)
        toolbar.background = GradientDrawable(
            GradientDrawable.Orientation.TOP_BOTTOM,
            intArrayOf(ColorUtils.blendARGB(color, Color.WHITE, if (dark) 0.07f else 0.22f), color),
        ).apply {
            cornerRadius = height / 2f
            setStroke(dp(1f).coerceAtLeast(1), ColorUtils.setAlphaComponent(Color.WHITE, if (dark) 70 else 230))
        }
        toolbar.elevation = dp(8f).toFloat()
        toolbar.navigationContentDescription = getString(R.string.back)
        toolbar.findViewById<TextView>(com.yalantis.ucrop.R.id.toolbar_title)?.apply {
            setTypeface(typeface, Typeface.BOLD)
            maxLines = 1
            ellipsize = TextUtils.TruncateAt.END
        }

        // Move safe insets outside the capsule; uCrop positions both canvas and loading guard below it.
        ViewCompat.setOnApplyWindowInsetsListener(toolbar) { view, insets ->
            val safe = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            val params = view.layoutParams as ViewGroup.MarginLayoutParams
            params.height = height
            params.setMargins(safe.left + dp(16f), safe.top + dp(8f), safe.right + dp(16f), dp(8f))
            view.layoutParams = params
            insets
        }
        ViewCompat.requestApplyInsets(toolbar)
    }
}
