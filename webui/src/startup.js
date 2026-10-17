if(/\/fullscreen\/?$/.test(location.pathname)){
  viewFullscreen();
}else{
paintTheme();
const topbar=$('.topbar');
const setChromeH=()=>document.documentElement.style.setProperty('--chrome-h',topbar.offsetHeight+'px');
setChromeH();
new ResizeObserver(setChromeH).observe(topbar);
(async()=>{
  try{
    const{data}=await api('/api/v1/device',{timeout:5000});
    setStats(data);
    S.ap=!data.ipAddress||data.ipAddress==='0.0.0.0';
  }catch(e){}
  render();
  try{
    await ensureCaps();
    //linux:begin
    if(S.oauth&&/^#oauth(_error)?=/.test(location.hash))oauthReturn();
    //linux:end
    nav();
  }catch(e){}
})();
}
