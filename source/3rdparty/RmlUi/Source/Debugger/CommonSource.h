/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

static const char* common_rcss = R"RCSS(
body
{
	font-family: rmlui-debugger-font;
	z-index: 1000000;
	font-size: 20dp;
	line-height: 1.4;
	color: black;
	padding-top: 2.3em;
}
div, h1, h2, h3, h4, p
{
	display: block;
}
em
{
	font-style: italic;
}
h1
{
	position: absolute;
	top: 0;
	right: 0;
	left: 0;
	height: 1.69em;
	padding: 4dp 5dp;
	color: white;
	background-color: #888;
	font-size: 1.15em;
}
h2
{
	background-color: #ddd;
	border-width: 1dp 0px;
	border-color: #888;
}
h3
{
	margin-top: 0.6em;
	color: red;
}
h4
{
	color: #cc0000;
}
handle#position_handle
{
	display: block;
	position: absolute;
	top: 0;
	right: 0;
	bottom: 0;
	left: 0;
}
h1 .button
{
	z-index: 1;
}
div#close_button
{
	margin-left: 0.92em;
	z-index: 1;
	float: right;
	width: 1.38em;
	color: black;
	background-color: #ddd;
	border-width: 1dp;
	border-color: #666;
	text-align: center;
}
div#close_button:hover
{
	background-color: #eee;
}
div#close_button:active
{
	background-color: #fff;
}
div#content
{
	position: relative;
	width: auto;
	height: 100%;
	overflow: auto;
	background: white;
	border-width: 2dp;
	border-color: #888;
	border-top-width: 0;
}
.error
{
	background: #d24040;
	color: white;
	border-color: #b74e4e;
}
.warning
{
	background: #e8d34e;
	color: black;
	border-color: #ca9466;
}
.info
{
	background: #2a9cdb;
	color: white;
	border-color: #3b70bb;
}
.debug
{
	background: #3fab2a;
	color: white;
	border-color: #226c13;
}
scrollbarvertical
{
	width: 1.23em;
	scrollbar-margin: 1.23em;
}
scrollbarhorizontal
{
	height: 1.23em;
	scrollbar-margin: 1.23em;
}
scrollbarvertical slidertrack,
scrollbarhorizontal slidertrack
{
	background: #aaa;
	border-color: #888;
}
scrollbarvertical slidertrack
{
	border-left-width: 1dp;
}
scrollbarhorizontal slidertrack
{
	height: 1.15em;
	border-top-width: 1dp;
}
scrollbarvertical sliderbar,
scrollbarhorizontal sliderbar
{
	background: #ddd;
	border-color: #888;
}
scrollbarvertical sliderbar
{
	border-width: 1dp 0;
	margin-left: 1dp;
}
scrollbarhorizontal sliderbar
{
	height: 1.15em;
	border-width: 0 1dp;
	margin-top: 1dp;
}
scrollbarcorner
{
	background: #888;
}
handle#size_handle
{
	position: absolute;
	width: 1.23em;
	height: 1.23em;
	bottom: 0dp;
	right: 2dp;
	background-color: #888;
}
)RCSS";
