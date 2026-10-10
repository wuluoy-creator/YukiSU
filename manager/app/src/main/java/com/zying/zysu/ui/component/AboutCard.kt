package com.zying.zysu.ui.component

import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.*
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.zying.zysu.BuildConfig
import com.zying.zysu.R
import com.zying.zysu.ui.theme.isExpressiveUi
import com.zying.zysu.ui.theme.getCardBorder

@Preview
@Composable
fun AboutCard() {
    val shape = if (isExpressiveUi) MaterialTheme.shapes.extraLarge else RoundedCornerShape(8.dp)
    ElevatedCard(
        modifier = Modifier.fillMaxWidth().border(getCardBorder(), shape),
        shape = shape,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp)
        ) {
            AboutCardContent()
        }
    }
}

@Composable
fun AboutDialog(dismiss: () -> Unit) {
    Dialog(
        onDismissRequest = { dismiss() }
    ) {
        Box(modifier = Modifier.clickHapticFeedback()) {
            AboutCard()
        }
    }
}

@Composable
private fun AboutCardContent() {
    Column(
        modifier = Modifier.fillMaxWidth()
    ) {
        Row {
            AppLogo(
                modifier = Modifier.size(40.dp)
            )

            Spacer(modifier = Modifier.width(12.dp))

            Column {

                Text(
                    stringResource(id = R.string.app_name),
                    style = if (isExpressiveUi) {
                        MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Normal)
                    } else {
                        MaterialTheme.typography.titleSmall
                    },
                    fontSize = if (isExpressiveUi) 22.sp else 18.sp
                )
                Text(
                    BuildConfig.VERSION_NAME,
                    style = MaterialTheme.typography.bodySmall,
                    fontSize = 14.sp
                )

                Spacer(modifier = Modifier.height(8.dp))

                val annotatedString = AnnotatedString.fromHtml(
                    htmlString = stringResource(
                        id = R.string.about_releases,
                        "<b><a href=\"https://github.com/wuluoy-creator/ZySU\">GitHub</a></b>",
                        "<b><a href=\"https://qun.qq.com/universal-share/share?ac=1&authKey=Xuczsi%2FuUDwLItMIXmPWROr7eqfRU0qszW9cceMbiUxvYNQ3DY2jZxOQOJ9%2B0tRH&busi_data=eyJncm91cENvZGUiOiIxODI2ODEyMzYiLCJ0b2tlbiI6IlNFWjgwd2ZVejlFRUU1Rk1yelo0SkM4WEVuUmJvUEY0Qnpjd1p4NFZ2UkF6T1NWQmcrUFRnT1JjM3hGdk05QVgiLCJ1aW4iOiIyOTk0MTMxODQ1In0%3D&data=2AxJZJ02w2wlYW9qKa4hSZBNUMzrIdamYW6cSmbiElJ_bGIN1rlhynhzLVVrRTrvikh_XykqOQ12PemXj4lsaQ&svctype=4&tempid=h5_group_info\">QQ</a></b>"
                    ),
                    linkStyles = TextLinkStyles(
                        style = SpanStyle(
                            color = MaterialTheme.colorScheme.primary,
                            textDecoration = TextDecoration.Underline
                        ),
                        pressedStyle = SpanStyle(
                            color = MaterialTheme.colorScheme.primary,
                            background = MaterialTheme.colorScheme.secondaryContainer,
                            textDecoration = TextDecoration.Underline
                        )
                    )
                )
                Text(
                    text = annotatedString,
                    style = TextStyle(
                        fontSize = 14.sp
                    )
                )
            }
        }
    }
}
