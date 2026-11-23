rate=44100
[['Snare',0.24],['OpenHat',0.65]].each do |name,duration|
 rng=Random.new(714); prev=0.0
 pcm=Array.new((rate*duration).to_i) do |i|
 t=i.to_f/rate; noise=rng.rand*2-1; hp=noise-prev; prev=noise
 value=if name=='Snare'
  0.35*noise*Math.exp(-t*20)+0.28*Math.sin(2*Math::PI*185*t)*Math.exp(-t*32)
 else
  0.22*hp*Math.exp(-t*7)*(1-Math.exp(-t*1500))
 end
 [[(value*32767).to_i,-32768].max,32767].min
 end.pack('s<*')
 fmt=[1,1,rate,rate*2,2,16].pack('vvVVvv')
 File.binwrite("Assets/Factory/#{name}.wav",'RIFF'+[36+pcm.bytesize].pack('V')+'WAVEfmt '+[16].pack('V')+fmt+'data'+[pcm.bytesize].pack('V')+pcm)
end
