# ESPlayer/AVS2-Player
<pre>
查看每一帧视频数据，类似Elecard StreamEye的工具。
目标是支持HEVC和AVS2 ES播放和分析，目前只支持AVS2 ES。
写的比较简陋，可以在Linux和windows双平台编译，使用。
</pre>
![image](https://github.com/xiejingcai/AVS2-Player/blob/master/ESPlayer.jpg)

# 引用项目
AVS2解码：https://github.com/pkuvcl/davs2
<pre>
暂时10bit色深码流解码不能使用MMX指令加速。
</pre>
显示界面：http://www.libsdl.org/
<pre>
SDL做图像/图形绘制和显示，SDL_ttf用于文字绘制。
</pre>
# 使用说明
拖拽视频文件到可执行程序图标上，或者在终端敲:
<pre>
> ESPlayer xxx.avs2。
</pre>
空格按键切换模式:
<pre>
stream play模式就是普通视频播放模式。

single frame模式，可以用鼠标点击，查看每一帧信息。
此模式下，可以通过鼠标中键滚动，浏览视频序列（sequence）分组（gop），鼠标左键点击选中感兴趣的分组，选择分组自动展开，左键点击查看每一帧。

详情请见网盘中视频演示：ESPlayer.mp4
</pre>
# 测试用例
https://pan.baidu.com/s/1Enrl8TKmqfFFvXuq5wYkCQ
<pre>
普通AVS2 ES ，可以单帧查看，也可以匀速播放：wild.avs2
部分AVS2码流支持不够好，只能匀速播放和暂停，后续会解决：chess.avs2，traveller.avs2
</pre>


